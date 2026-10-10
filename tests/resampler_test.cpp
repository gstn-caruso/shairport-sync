#include "session/session_state.hpp"
#include "audio/resampling/resampler.hpp"
#include "audio/output/audio_player_adapter.hpp"
#include <gtest/gtest.h>
#include <stdexcept>
#include <cstdlib>
#include <cstring>
#include <array>
#include <algorithm>
#include <vector>

static unsigned initialized, released;
static bool observeFlush;
static int pendingBeforeReset;
extern "C" int __real_swr_init(SwrContext *);
extern "C" void __real_swr_free(SwrContext **);
extern "C" int __wrap_swr_init(SwrContext *context) {
  if (observeFlush)
    pendingBeforeReset = swr_get_out_samples(context, 0);
  ++initialized;
  return __real_swr_init(context);
}
extern "C" void __wrap_swr_free(SwrContext **context) {
  if (context && *context)
    ++released;
  __real_swr_free(context);
}

static OwnedAudioFrame samplesFor(AudioFormat format, AVSampleFormat sampleFormat) {
  OwnedAudioFrame frame(av_frame_alloc());
  if (!frame)
    throw std::runtime_error("Cannot allocate the resampler input frame");
  frame->format = sampleFormat;
  frame->sample_rate = format.sampleRate();
  frame->nb_samples = 256;
  av_channel_layout_default(&frame->ch_layout, format.channels());
  const auto allocated = av_frame_get_buffer(frame.get(), 0);
  EXPECT_EQ(allocated, 0);
  if (allocated != 0)
    throw std::runtime_error("Cannot allocate the resampler input samples");
  for (unsigned channel = 0; channel < format.channels(); ++channel)
    for (int sample = 0; sample < frame->nb_samples; ++sample)
      if (sampleFormat == AV_SAMPLE_FMT_S16P)
        reinterpret_cast<int16_t *>(frame->extended_data[channel])[sample] = 512 * (channel + 1);
      else if (sampleFormat == AV_SAMPLE_FMT_S32P)
        reinterpret_cast<int32_t *>(frame->extended_data[channel])[sample] = 0x1000000 * (channel + 1);
      else
        reinterpret_cast<float *>(frame->extended_data[channel])[sample] = 0.0625f * (channel + 1);
  return frame;
}

static void checkNativeFormat(ssrc_t encoding) {
  auto format = *AudioFormat::fromSsrc(encoding);
  const auto sampleFormat = format.isAac() ? AV_SAMPLE_FMT_FLTP :
      encoding == ALAC_44100_S16_2 ? AV_SAMPLE_FMT_S16P : AV_SAMPLE_FMT_S32P;
  auto frame = samplesFor(format, sampleFormat);
  for (unsigned rate : {format.sampleRate(), format.sampleRate() == 44100 ? 48000U : 44100U}) {
    SCOPED_TRACE(testing::Message() << "Output rate " << rate);
    Resampler resampler;
    OutputFormat output{rate, format.channels()};
    ASSERT_TRUE(resampler.configure(format, sampleFormat, output));
    auto converted = resampler.convert(*frame);
    ASSERT_TRUE(converted.has_value());
    EXPECT_GT(converted->frames(), 0);
    EXPECT_EQ(converted->shape(), NativePcmShape(output.channels, resampler.sampleBits(),
                                               resampler.effectiveSampleBits()));
    EXPECT_EQ(converted->bytes().size(), converted->frames() * output.channels *
                                       resampler.sampleBits() / 8);
    EXPECT_EQ(resampler.effectiveSampleBits(), (format.isAac() ? 32 : format.sampleBits()));
    if (rate == format.sampleRate()) {
      ASSERT_EQ(converted->frames(), 256);
      EXPECT_EQ(converted->retainedFrames(), 0);
      ASSERT_GE(converted->bytes().size(), resampler.sampleBits() / 8);
      if (resampler.sampleBits() == 16)
        EXPECT_EQ(reinterpret_cast<const int16_t *>(converted->bytes().data())[0], 512);
      else
        EXPECT_EQ(reinterpret_cast<const int32_t *>(converted->bytes().data())[0],
               (format.isAac() ? 0x8000000 : 0x1000000));
    } else {
      EXPECT_GT(converted->retainedFrames(), 0);
      const auto previousInitialization = initialized;
      const auto retained = resampler.retainedFrames();
      EXPECT_EQ(resampler.configure(format, sampleFormat, output), ResamplerChange::unchanged);
      EXPECT_EQ(initialized, previousInitialization);
      EXPECT_EQ(resampler.retainedFrames(), retained);
      observeFlush = true;
      auto pending = resampler.flush();
      observeFlush = false;
      ASSERT_TRUE(pending.has_value());
      EXPECT_EQ(*pending, static_cast<size_t>(pendingBeforeReset));
      EXPECT_GT(*pending, 0);
      EXPECT_EQ(resampler.retainedFrames(), 0);
    }
  }
}

TEST(Resampler, UnconfiguredSilenceFailsAndFlushHasNoPendingFrames) {
  Resampler resampler;
  EXPECT_FALSE(resampler.silence(64));
  EXPECT_EQ(resampler.flush(), 0);
}

TEST(Resampler, StereoReconfigurationAfterMonoConversionProducesSilentFrames) {
  Resampler resampler;
  auto format = *AudioFormat::fromSsrc(ALAC_44100_S16_2);
  ASSERT_TRUE(resampler.configure(format, AV_SAMPLE_FMT_S16P, {44100, 1, 0, true}));
  auto mono = resampler.convert(*samplesFor(format, AV_SAMPLE_FMT_S16P));
  ASSERT_TRUE(mono.has_value());
  EXPECT_EQ(mono->frames(), 256);
  EXPECT_EQ(mono->bytes().size(), 512);
  ASSERT_EQ(resampler.configure(format, AV_SAMPLE_FMT_S16P, {44100, 2}), ResamplerChange::changed);
  auto silence = resampler.silence(64);
  ASSERT_TRUE(silence.has_value());
  EXPECT_EQ(silence->frames(), 64);
  EXPECT_EQ(silence->retainedFrames(), 0);
  EXPECT_EQ(std::vector<uint8_t>(silence->bytes().begin(), silence->bytes().end()),
            std::vector<uint8_t>(silence->bytes().size(), 0));
}

TEST(Resampler, InvalidConfigurationPreservesContextAndRepeatedResetReleasesItOnce) {
  Resampler resampler;
  auto format = *AudioFormat::fromSsrc(ALAC_44100_S16_2);
  ASSERT_TRUE(resampler.configure(format, AV_SAMPLE_FMT_S16P, {44100, 2}));
  const auto previousRelease = released;
  EXPECT_FALSE(resampler.configure(format, AV_SAMPLE_FMT_S16P, {0, 2}));
  EXPECT_EQ(released, previousRelease);
  EXPECT_TRUE(resampler.configuredFor(format));
  auto wrong = samplesFor(format, AV_SAMPLE_FMT_S32P);
  EXPECT_FALSE(resampler.convert(*wrong));
  resampler.reset();
  EXPECT_EQ(released, previousRelease + 1);
  resampler.reset();
  EXPECT_EQ(released, previousRelease + 1);
}

TEST(Resampler, SilenceKeepsRateConversionContinuousWithDirectFfmpegReference) {
  const auto format = *AudioFormat::fromSsrc(ALAC_44100_S16_2);
  auto frame = samplesFor(format, AV_SAMPLE_FMT_S16P);
  Resampler resampler;
  ASSERT_TRUE(resampler.configure(format, AV_SAMPLE_FMT_S16P, {48000, 2}));
  AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
  SwrContext *native = nullptr;
  ASSERT_EQ(swr_alloc_set_opts2(&native, &stereo, AV_SAMPLE_FMT_S16, 48000,
                            &stereo, AV_SAMPLE_FMT_S16P, 44100, 0, nullptr), 0);
  const auto freeContext = [](SwrContext *context) { swr_free(&context); };
  std::unique_ptr<SwrContext, decltype(freeContext)> reference(native, freeContext);
  ASSERT_EQ(swr_init(reference.get()), 0);
  const auto convertReference = [&](const uint8_t **input, int count) {
    const int capacity = swr_get_out_samples(reference.get(), count);
    EXPECT_GE(capacity, 0);
    if (capacity < 0)
      return std::vector<uint8_t>{};
    std::vector<uint8_t> bytes(static_cast<size_t>(capacity) * 4);
    uint8_t *output = bytes.data();
    const int generated = swr_convert(reference.get(), &output, capacity, input, count);
    EXPECT_GE(generated, 0);
    if (generated < 0)
      return std::vector<uint8_t>{};
    bytes.resize(static_cast<size_t>(generated) * 4);
    return bytes;
  };
  const auto matches = [](const ConvertedAudio &actual, const std::vector<uint8_t> &expected) {
    EXPECT_EQ(actual.frames(), expected.size() / 4);
    EXPECT_EQ(std::vector<uint8_t>(actual.bytes().begin(), actual.bytes().end()), expected);
  };
  std::array<const uint8_t *, 2> input{frame->extended_data[0], frame->extended_data[1]};
  auto initial = resampler.convert(*frame);
  ASSERT_TRUE(initial.has_value());
  EXPECT_GT(initial->retainedFrames(), 0);
  matches(*initial, convertReference(input.data(), frame->nb_samples));
  ASSERT_EQ(swr_inject_silence(reference.get(), 64), 0);
  std::array<const uint8_t *, 2> empty{};
  auto silence = resampler.silence(64);
  ASSERT_TRUE(silence.has_value());
  matches(*silence, convertReference(empty.data(), 0));
  auto following = resampler.convert(*frame);
  ASSERT_TRUE(following.has_value());
  matches(*following, convertReference(input.data(), frame->nb_samples));
}

int setup_software_resampler(rtsp_conn_info *, ssrc_t);
void clear_software_resampler(rtsp_conn_info *);
static int32_t chooseStereo(unsigned, unsigned, unsigned) {
  return CHANNELS_TO_ENCODED_FORMAT(2) | RATE_TO_ENCODED_FORMAT(44100) |
         FORMAT_TO_ENCODED_FORMAT(SPS_FORMAT_S16_LE);
}
static int32_t rejectOutput(unsigned, unsigned, unsigned) { return 0; }
static int configureBorrowedChannelMap(int32_t, char **channelMap) {
  static char channels[] = "FR FL";
  *channelMap = channels;
  return 0;
}

class PlayerResampler : public testing::Test {
  decltype(config.output) savedOutput = config.output;
  decltype(config.current_output_configuration) savedOutputConfiguration = config.current_output_configuration;
  decltype(config.output_channel_mapping_enable) savedMappingEnabled = config.output_channel_mapping_enable;
  decltype(config.output_channel_map_size) savedMapSize = config.output_channel_map_size;
  std::array<const char *, 8> savedChannelMap;
protected:
  PlayerResampler() {
    std::copy(std::begin(config.output_channel_map), std::end(config.output_channel_map),
              savedChannelMap.begin());
  }
  ~PlayerResampler() override {
    config.output = savedOutput;
    config.current_output_configuration = savedOutputConfiguration;
    config.output_channel_mapping_enable = savedMappingEnabled;
    config.output_channel_map_size = savedMapSize;
    std::copy(savedChannelMap.begin(), savedChannelMap.end(), std::begin(config.output_channel_map));
  }
};

static OwnedAudioFrame stereoFrame() {
  auto frame = samplesFor(*AudioFormat::fromSsrc(ALAC_44100_S16_2), AV_SAMPLE_FMT_S16P);
  frame->nb_samples = 64;
  std::fill_n(reinterpret_cast<int16_t *>(frame->data[0]), 64, 5);
  std::fill_n(reinterpret_cast<int16_t *>(frame->data[1]), 64, 9);
  return frame;
}

TEST_F(PlayerResampler, NegotiationPreservesMappingAndOwnedStateOnRejection) {
  config.output_channel_mapping_enable = 0;
  config.output_channel_map_size = 0;
  audio_output backend{};
  backend.get_configuration = chooseStereo;
  config.output = &backend;
  SessionState session{};
  prepare_decoding_chain(&session, ALAC_44100_S16_2);
  ASSERT_EQ(setup_software_resampler(&session, ALAC_44100_S16_2), 0);
  EXPECT_EQ(session.resampler.outputShape(), NativePcmShape(2, 16, 16));
  auto frame = stereoFrame();
  auto initial = convertIncomingAudio(session, *frame);
  EXPECT_EQ(initial.retainedFrames(), 0);
  EXPECT_EQ(initial.frames(), 64);
  ASSERT_EQ(initial.bytes().size(), 256);
  auto *result = reinterpret_cast<int16_t *>(initial.bytes().data());
  EXPECT_EQ(result[0], 5);
  EXPECT_EQ(result[1], 9);
  initial.reset();
  config.output_channel_mapping_enable = 1;
  backend.configure = configureBorrowedChannelMap;
  for (unsigned attempt = 0; attempt < 2; ++attempt) {
    SCOPED_TRACE(testing::Message() << "Device mapping attempt " << attempt);
    ASSERT_EQ(setup_software_resampler(&session, ALAC_44100_S16_2), 0);
    auto deviceMapped = convertIncomingAudio(session, *frame);
    EXPECT_EQ(deviceMapped.frames(), 64);
    ASSERT_EQ(deviceMapped.bytes().size(), 256);
    auto *samples = reinterpret_cast<const int16_t *>(deviceMapped.bytes().data());
    EXPECT_EQ(samples[0], 9);
    EXPECT_EQ(samples[1], 5);
  }
  backend.configure = nullptr;
  config.output_channel_map_size = 2;
  config.output_channel_map[0] = "FM";
  config.output_channel_map[1] = "--";
  ASSERT_EQ(setup_software_resampler(&session, ALAC_44100_S16_2), 0);
  auto mapped = convertIncomingAudio(session, *frame);
  ASSERT_EQ(mapped.bytes().size(), 256);
  result = reinterpret_cast<int16_t *>(mapped.bytes().data());
  EXPECT_EQ(result[0], 6);
  EXPECT_EQ(result[1], 0);
  mapped.reset();
  backend.get_configuration = rejectOutput;
  const auto previousRate = session.inputAudio.sampleRate(), previousFrames = session.inputAudio.framesPerPacket();
  const auto previousConfiguration = config.current_output_configuration;
  setup_software_resampler(&session, ALAC_48000_S24_2);
  EXPECT_EQ(session.inputAudio.sampleRate(), previousRate);
  EXPECT_EQ(session.inputAudio.framesPerPacket(), previousFrames);
  EXPECT_EQ(config.current_output_configuration, previousConfiguration);
  clear_software_resampler(&session);
}

TEST(Resampler, UnchangedConfigurationPreservesSamplesAndUsedStateResetIsIdempotent) {
  auto frame = stereoFrame();
  Resampler resampler;
  auto format = *AudioFormat::fromSsrc(ALAC_44100_S16_2);
  OutputFormat output{44100, 2};
  ASSERT_EQ(resampler.configure(format, AV_SAMPLE_FMT_S16P, output), ResamplerChange::changed);
  auto converted = resampler.convert(*frame);
  ASSERT_TRUE(converted.has_value());
  EXPECT_EQ(converted->frames(), 64);
  ASSERT_EQ(converted->bytes().size(), 256);
  EXPECT_EQ(converted->retainedFrames(), 0);
  auto pcm = reinterpret_cast<const int16_t *>(converted->bytes().data());
  EXPECT_EQ(pcm[0], 5);
  EXPECT_EQ(pcm[1], 9);
  EXPECT_EQ(resampler.configure(format, AV_SAMPLE_FMT_S16P, output), ResamplerChange::unchanged);
  EXPECT_TRUE(resampler.configuredFor(format));
  EXPECT_EQ(resampler.sampleBits(), 16);
  EXPECT_EQ(resampler.effectiveSampleBits(), 16);
  resampler.reset();
  resampler.reset();
  EXPECT_FALSE(resampler.configuredFor(format));
}

TEST(Resampler, Alac44100StereoPreservesNativeSamplesRetentionAndPendingResetCount) {
  checkNativeFormat(ALAC_44100_S16_2);
}

TEST(Resampler, Alac48000StereoPreservesNative24BitShapeRetentionAndPendingResetCount) {
  checkNativeFormat(ALAC_48000_S24_2);
}

TEST(Resampler, Aac44100StereoPreservesNativeSamplesRetentionAndPendingResetCount) {
  checkNativeFormat(AAC_44100_F24_2);
}

TEST(Resampler, Aac48000StereoPreservesNativeSamplesRetentionAndPendingResetCount) {
  checkNativeFormat(AAC_48000_F24_2);
}

TEST(Resampler, Aac48000Surround51PreservesShapeRetentionAndPendingResetCount) {
  checkNativeFormat(AAC_48000_F24_5P1);
}

TEST(Resampler, Aac48000Surround71PreservesShapeRetentionAndPendingResetCount) {
  checkNativeFormat(AAC_48000_F24_7P1);
}
