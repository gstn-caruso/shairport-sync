#include "session/session_state.hpp"
#include "audio/output/audio_player_adapter.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <stdexcept>

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

TEST(AudioDecoder, PlayerBoundaryPreservesKnownFormatAndClearsIdempotently) {
  SessionState session{};
  prepare_decoding_chain(&session, ALAC_44100_S16_2);
  EXPECT_EQ(session.inputAudio.sampleRate(), 44100);
  EXPECT_EQ(session.inputAudio.framesPerPacket(), 352);
  EXPECT_TRUE(session.inputAudio.isDecodedFormatValid());
  prepare_decoding_chain(&session, static_cast<ssrc_t>(0xf00d));
  ASSERT_TRUE(session.decoder.currentFormat().has_value());
  EXPECT_EQ(session.decoder.currentFormat()->ssrc(), ALAC_44100_S16_2);
  EXPECT_EQ(session.inputAudio.sampleRate(), 44100);
  std::array<uint8_t, 8> shortPacket{};
  EXPECT_EQ(block_to_avframe(&session, shortPacket.data(), shortPacket.size()), nullptr);
  std::array<uint8_t, 16> invalidPacket{};
  EXPECT_EQ(block_to_avframe(&session, invalidPacket.data(), invalidPacket.size()), nullptr);
  clear_decoding_chain(&session);
  clear_decoding_chain(&session);
  EXPECT_FALSE(session.decoder.currentFormat());
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

static int configureBorrowedMapDespiteError(int32_t encoded, char **channelMap) {
  configureBorrowedChannelMap(encoded, channelMap);
  return -1;
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
  EXPECT_EQ(session.pcmEncoder.silence(1, DitherPolicy::disabled).bytes().size(), 4u);
  clear_software_resampler(&session);
}

TEST_F(PlayerResampler, MissingBackendChoiceUsesStereo48kS32LittleEndian) {
  config.output_channel_mapping_enable = 0;
  config.output_channel_map_size = 0;
  audio_output backend{};
  config.output = &backend;
  SessionState session{};
  ASSERT_EQ(setup_software_resampler(&session, AAC_48000_F24_2), 0);
  EXPECT_EQ(config.current_output_configuration,
            CHANNELS_TO_ENCODED_FORMAT(2) | RATE_TO_ENCODED_FORMAT(48000) |
            FORMAT_TO_ENCODED_FORMAT(SPS_FORMAT_S32_LE));
  EXPECT_EQ(session.resampler.outputShape(), NativePcmShape(2, 32, 32));
  EXPECT_EQ(session.inputAudio.sampleRate(), 48000u);
  EXPECT_EQ(session.pcmEncoder.silence(1, DitherPolicy::disabled).bytes().size(), 8u);
}

TEST_F(PlayerResampler, BackendConfigureErrorStillUsesItsBorrowedChannelMap) {
  config.output_channel_mapping_enable = 1;
  config.output_channel_map_size = 0;
  audio_output backend{};
  backend.get_configuration = chooseStereo;
  backend.configure = configureBorrowedMapDespiteError;
  config.output = &backend;
  SessionState session{};
  prepare_decoding_chain(&session, ALAC_44100_S16_2);
  ASSERT_EQ(setup_software_resampler(&session, ALAC_44100_S16_2), 0);
  auto frame = stereoFrame();
  auto converted = convertIncomingAudio(session, *frame);
  ASSERT_EQ(converted.frames(), 64u);
  ASSERT_EQ(converted.bytes().size(), 256u);
  const auto *samples = reinterpret_cast<const int16_t *>(converted.bytes().data());
  EXPECT_EQ(samples[0], 9);
  EXPECT_EQ(samples[1], 5);
}
