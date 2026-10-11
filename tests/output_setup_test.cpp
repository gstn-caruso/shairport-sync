#include "audio/output/output_setup.hpp"
#include "audio/format/encoded_output_format.h"
#include <gtest/gtest.h>
#include <cerrno>
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>

static bool failResamplerInitialization = false;
extern "C" int __real_swr_init(SwrContext *);
extern "C" int __wrap_swr_init(SwrContext *context) {
  return failResamplerInitialization ? AVERROR(EINVAL) : __real_swr_init(context);
}

namespace {
constexpr uint32_t outputFormat(unsigned channels, unsigned rate, sps_format_t format) {
  return CHANNELS_TO_ENCODED_FORMAT(channels) | RATE_TO_ENCODED_FORMAT(rate) |
         FORMAT_TO_ENCODED_FORMAT(format);
}

class OutputBackend : public OutputSetupPort {
public:
  std::optional<uint32_t> choice;
  std::string deviceNames;
  std::vector<uint32_t> configured;
  std::function<void()> onConfigure;
  std::optional<uint32_t> choose(AudioFormat) override { return choice; }
  std::string configure(uint32_t encoded) override {
    configured.push_back(encoded);
    if (onConfigure) onConfigure();
    return deviceNames;
  }
};

auto samplesFor(AudioFormat format, AVSampleFormat sampleFormat) {
  const auto release = [](AVFrame *frame) { av_frame_free(&frame); };
  std::unique_ptr<AVFrame, decltype(release)> frame(av_frame_alloc(), release);
  if (!frame) throw std::runtime_error("Cannot allocate input frame");
  frame->format = sampleFormat;
  frame->sample_rate = format.sampleRate();
  frame->nb_samples = 64;
  av_channel_layout_default(&frame->ch_layout, format.channels());
  if (av_frame_get_buffer(frame.get(), 0) != 0)
    throw std::runtime_error("Cannot allocate input samples");
  for (unsigned channel = 0; channel < format.channels(); ++channel)
    for (int sample = 0; sample < frame->nb_samples; ++sample)
      if (sampleFormat == AV_SAMPLE_FMT_S16P)
        reinterpret_cast<int16_t *>(frame->extended_data[channel])[sample] = 5 + channel * 4;
      else if (sampleFormat == AV_SAMPLE_FMT_S32P)
        reinterpret_cast<int32_t *>(frame->extended_data[channel])[sample] = 0x1000000 * (channel + 1);
      else
        reinterpret_cast<float *>(frame->extended_data[channel])[sample] = 0.03125f * (channel + 1);
  return frame;
}

class OutputSetup : public testing::Test {
protected:
  OutputBackend backend;
  Resampler resampler;
  PcmEncoder pcm{[] { return 0; }};
  std::vector<uint32_t> published;
  NativePcmShape shapeAtPublication;
  size_t pcmBytesAtPublication = 0;
  const AudioFormat alac = *AudioFormat::fromSsrc(ALAC_44100_S16_2);

  void SetUp() override { ASSERT_TRUE(pcm.configure({SPS_FORMAT_S16_LE, 2}, 16)); }
  void TearDown() override { failResamplerInitialization = false; }
  OutputSetupCoordinator coordinator(OutputSetupSettings settings = {}) {
    return {std::move(settings), backend, resampler, pcm, [&](uint32_t encoded, AudioFormat) {
      published.push_back(encoded);
      shapeAtPublication = resampler.outputShape();
      pcmBytesAtPublication = pcm.silence(1, DitherPolicy::disabled).bytes().size();
    }};
  }
};

TEST_F(OutputSetup, MissingChoiceUsesStereo48kS32LittleEndian) {
  auto setup = coordinator();
  const auto input = *AudioFormat::fromSsrc(AAC_48000_F24_2);
  EXPECT_EQ(setup.configure(input), outputFormat(2, 48000, SPS_FORMAT_S32_LE));
  EXPECT_EQ(backend.configured, std::vector{outputFormat(2, 48000, SPS_FORMAT_S32_LE)});
  EXPECT_EQ(resampler.outputShape(), NativePcmShape(2, 32, 32));
  EXPECT_EQ(pcm.silence(1, DitherPolicy::disabled).bytes().size(), 8u);
}

TEST_F(OutputSetup, RejectedChoicePreservesResamplerPcmAndPublication) {
  backend.choice = outputFormat(2, 44100, SPS_FORMAT_S16_LE);
  auto setup = coordinator();
  ASSERT_TRUE(setup.configure(alac, AV_SAMPLE_FMT_S16P));
  backend.choice = 0;
  const auto rejected = setup.configure(*AudioFormat::fromSsrc(AAC_48000_F24_2));
  ASSERT_FALSE(rejected);
  EXPECT_EQ(rejected.error().kind, OutputSetupFailure::Kind::backendRejected);
  EXPECT_TRUE(resampler.configuredFor(alac));
  EXPECT_EQ(published.size(), 1u);
  EXPECT_EQ(backend.configured.size(), 1u);
  EXPECT_EQ(pcm.silence(1, DitherPolicy::disabled).bytes().size(), 4u);
}

TEST_F(OutputSetup, BackendConfigurationPrecedesResamplerAndPublicationPrecedesPcm) {
  backend.choice = outputFormat(2, 44100, SPS_FORMAT_S32_LE);
  backend.onConfigure = [&] { EXPECT_EQ(resampler.outputShape(), NativePcmShape{}); };
  auto setup = coordinator();
  ASSERT_TRUE(setup.configure(alac, AV_SAMPLE_FMT_S16P));
  EXPECT_EQ(shapeAtPublication, NativePcmShape(2, 16, 16));
  EXPECT_EQ(pcmBytesAtPublication, 4u);
  EXPECT_EQ(pcm.silence(1, DitherPolicy::disabled).bytes().size(), 8u);
}

TEST_F(OutputSetup, OwnedDeviceMapReversesTheProducedStereoSamples) {
  backend.choice = outputFormat(2, 44100, SPS_FORMAT_S16_LE);
  backend.deviceNames = "FR FL";
  auto setup = coordinator({.mappingEnabled = true});
  ASSERT_TRUE(setup.configure(alac, AV_SAMPLE_FMT_S16P));
  backend.deviceNames = "changed after setup";
  auto frame = samplesFor(alac, AV_SAMPLE_FMT_S16P);
  auto converted = resampler.convert(*frame);
  ASSERT_TRUE(converted);
  ASSERT_EQ(converted->bytes().size(), 256u);
  const auto *samples = reinterpret_cast<const int16_t *>(converted->bytes().data());
  EXPECT_EQ(samples[0], 9);
  EXPECT_EQ(samples[1], 5);
}

TEST_F(OutputSetup, CoordinatorOwnsCallerSettingsAfterTheirSourceChanges) {
  backend.choice = outputFormat(2, 44100, SPS_FORMAT_S16_LE);
  OutputSetupSettings settings{.mappingEnabled = true, .channelNames = {"FM", "--"}};
  auto setup = coordinator(settings);
  settings.channelNames = {"FR", "FL"};
  ASSERT_TRUE(setup.configure(alac, AV_SAMPLE_FMT_S16P));
  auto converted = resampler.convert(*samplesFor(alac, AV_SAMPLE_FMT_S16P));
  ASSERT_TRUE(converted);
  const auto *samples = reinterpret_cast<const int16_t *>(converted->bytes().data());
  EXPECT_EQ(samples[0], 6);
  EXPECT_EQ(samples[1], 0);
}

TEST_F(OutputSetup, ExplicitDecodedFormatOverridesAacAndDecoderFallback) {
  const auto input = *AudioFormat::fromSsrc(AAC_48000_F24_2);
  backend.choice = outputFormat(2, 48000, SPS_FORMAT_S32_LE);
  auto setup = coordinator();
  ASSERT_TRUE(setup.configure(input, AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S32P));
  EXPECT_TRUE(resampler.convert(*samplesFor(input, AV_SAMPLE_FMT_S16P)));
  EXPECT_FALSE(resampler.convert(*samplesFor(input, AV_SAMPLE_FMT_FLTP)));
}

TEST_F(OutputSetup, AacUsesFloatPlanarDespiteAnAvailableDecoderFormat) {
  const auto input = *AudioFormat::fromSsrc(AAC_48000_F24_2);
  auto setup = coordinator();
  ASSERT_TRUE(setup.configure(input, AV_SAMPLE_FMT_NONE, AV_SAMPLE_FMT_S16P));
  EXPECT_TRUE(resampler.convert(*samplesFor(input, AV_SAMPLE_FMT_FLTP)));
  EXPECT_FALSE(resampler.convert(*samplesFor(input, AV_SAMPLE_FMT_S16P)));
}

TEST_F(OutputSetup, AlacUsesDecoderFormatOrFloatPlanarWhenNoneIsAvailable) {
  backend.choice = outputFormat(2, 44100, SPS_FORMAT_S16_LE);
  auto setup = coordinator();
  ASSERT_TRUE(setup.configure(alac, AV_SAMPLE_FMT_NONE, AV_SAMPLE_FMT_S16P));
  EXPECT_TRUE(resampler.convert(*samplesFor(alac, AV_SAMPLE_FMT_S16P)));
  ASSERT_TRUE(setup.configure(alac));
  EXPECT_TRUE(resampler.convert(*samplesFor(alac, AV_SAMPLE_FMT_FLTP)));
  EXPECT_FALSE(resampler.convert(*samplesFor(alac, AV_SAMPLE_FMT_S16P)));
}

TEST_F(OutputSetup, FailedResamplerInitializationKeepsPublicationAndPcmUnchanged) {
  backend.choice = outputFormat(2, 44100, SPS_FORMAT_S16_LE);
  auto setup = coordinator();
  ASSERT_TRUE(setup.configure(alac, AV_SAMPLE_FMT_S16P));
  failResamplerInitialization = true;
  backend.choice = outputFormat(2, 48000, SPS_FORMAT_S32_LE);
  const auto failed = setup.configure(alac, AV_SAMPLE_FMT_S16P);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error().kind, OutputSetupFailure::Kind::resamplerFailed);
  ASSERT_TRUE(failed.error().resamplerFailure);
  EXPECT_EQ(failed.error().resamplerFailure->nativeCode, AVERROR(EINVAL));
  EXPECT_EQ(backend.configured.size(), 2u);
  EXPECT_EQ(published.size(), 1u);
  EXPECT_EQ(resampler.outputShape(), NativePcmShape(2, 16, 16));
  EXPECT_EQ(pcm.silence(1, DitherPolicy::disabled).bytes().size(), 4u);
}

TEST_F(OutputSetup, UnsupportedPcmRetainsResamplerAndPublicationWithPreviousPcm) {
  backend.choice = outputFormat(1, 44100, SPS_FORMAT_UNKNOWN);
  auto setup = coordinator();
  const auto failed = setup.configure(alac, AV_SAMPLE_FMT_S16P);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error().kind, OutputSetupFailure::Kind::unsupportedPcm);
  EXPECT_EQ(resampler.outputShape(), NativePcmShape(1, 16, 16));
  EXPECT_EQ(published, std::vector{*backend.choice});
  EXPECT_EQ(shapeAtPublication, NativePcmShape(1, 16, 16));
  EXPECT_EQ(pcm.silence(1, DitherPolicy::disabled).bytes().size(), 4u);
}

TEST_F(OutputSetup, SixAndEightChannelLayoutsUseOwnedMappingAndStereoMixdown) {
  for (const auto encoding : {AAC_48000_F24_5P1, AAC_48000_F24_7P1}) {
    SCOPED_TRACE(encoding);
    const auto input = *AudioFormat::fromSsrc(encoding);
    backend.choice = outputFormat(2, 48000, SPS_FORMAT_S32_LE);
    auto setup = coordinator({.sixChannelLayout = AV_CH_LAYOUT_5POINT1_BACK,
                              .eightChannelLayout = AV_CH_LAYOUT_7POINT1_WIDE,
                              .mixdown = true, .mixdownLayout = AV_CH_LAYOUT_STEREO});
    ASSERT_TRUE(setup.configure(input));
    auto frame = samplesFor(input, AV_SAMPLE_FMT_FLTP);
    auto converted = resampler.convert(*frame);
    ASSERT_TRUE(converted);
    EXPECT_EQ(converted->shape(), NativePcmShape(2, 32, 32));
    EXPECT_EQ(converted->frames(), 64u);
    EXPECT_EQ(converted->bytes().size(), 512u);
    EXPECT_EQ(pcm.silence(1, DitherPolicy::disabled).bytes().size(), 8u);
    AVChannelLayout source{};
    ASSERT_EQ(av_channel_layout_from_mask(&source, input.channels() == 6 ?
              AV_CH_LAYOUT_5POINT1_BACK : AV_CH_LAYOUT_7POINT1_WIDE), 0);
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    SwrContext *native = nullptr;
    const auto allocated = swr_alloc_set_opts2(&native, &stereo, AV_SAMPLE_FMT_S32, 48000,
                                               &source, AV_SAMPLE_FMT_FLTP, 48000, 0, nullptr);
    av_channel_layout_uninit(&source);
    const auto release = [](SwrContext *context) { swr_free(&context); };
    std::unique_ptr<SwrContext, decltype(release)> reference(native, release);
    ASSERT_EQ(allocated, 0);
    ASSERT_EQ(swr_init(reference.get()), 0);
    std::vector<const uint8_t *> planes(frame->extended_data,
                                        frame->extended_data + input.channels());
    std::vector<uint8_t> expected(512);
    auto *output = expected.data();
    ASSERT_EQ(swr_convert(reference.get(), &output, 64, planes.data(), 64), 64);
    EXPECT_EQ(std::vector<uint8_t>(converted->bytes().begin(), converted->bytes().end()), expected);
  }
}

TEST_F(OutputSetup, ExplicitMonoMixdownLayoutProducesOneSignalAndOneSilentChannel) {
  backend.choice = outputFormat(2, 44100, SPS_FORMAT_S16_LE);
  auto setup = coordinator({.mixdown = true, .mixdownLayout = AV_CH_LAYOUT_MONO});
  ASSERT_TRUE(setup.configure(alac, AV_SAMPLE_FMT_S16P));
  auto converted = resampler.convert(*samplesFor(alac, AV_SAMPLE_FMT_S16P));
  ASSERT_TRUE(converted);
  const auto *samples = reinterpret_cast<const int16_t *>(converted->bytes().data());
  AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
  AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
  std::array<double, 64 * 64> normalizedMatrix{};
  ASSERT_EQ(swr_build_matrix2(&stereo, &mono, std::sqrt(0.5), std::sqrt(0.5), 0, 1, 1,
                             normalizedMatrix.data(), 64, AV_MATRIX_ENCODING_NONE, nullptr), 0);
  EXPECT_DOUBLE_EQ(normalizedMatrix[0], 0.5);
  EXPECT_DOUBLE_EQ(normalizedMatrix[1], 0.5);
  EXPECT_EQ(samples[0], 5 * normalizedMatrix[0] + 9 * normalizedMatrix[1]);
  EXPECT_EQ(samples[0], 7);
  EXPECT_EQ(samples[1], 0);
}
}
