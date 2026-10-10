#include "audio/pcm/playback_samples.hpp"
#include <gtest/gtest.h>
#include <cstring>
#include <bit>
#include <vector>

static int32_t sampleAt(const EncodedPcm &audio, size_t sample) {
  if (sample >= audio.bytes().size() / 4) {
    ADD_FAILURE() << "Sample " << sample << " is outside " << audio.bytes().size() / 4 << " samples";
    return 0;
  }
  const auto bytes = audio.bytes().subspan(sample * 4, 4);
  const uint32_t value = uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 |
                         uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
  return std::bit_cast<int32_t>(value);
}
template <typename Sample>
static ConvertedAudio nativeAudio(std::vector<Sample> values, unsigned channels,
                                  unsigned effectiveBits) {
  auto audio = *ConvertedAudio::allocate(values.size() * sizeof(Sample), values.size() / channels,
                                         0, {channels, sizeof(Sample) * 8, effectiveBits});
  std::memcpy(audio.bytes().data(), values.data(), values.size() * sizeof(Sample));
  return audio;
}
static EncodedPcm encode(PlaybackSamples &samples, PcmEncoder &encoder,
                         const ConvertedAudio &audio, PlaybackMode mode, Correction correction) {
  const auto prepared = samples.prepare(audio, mode);
  EXPECT_TRUE(prepared);
  const auto configured = encoder.configure({SPS_FORMAT_S32_LE, audio.shape().channels()}, audio.shape().effectiveBits());
  EXPECT_TRUE(configured);
  if (!prepared || !configured)
    return {};
  encoder.beginFrame(0x10000, mode == PlaybackMode::mono);
  return samples.encode(encoder, correction);
}

static constexpr std::array modes{PlaybackMode::stereo, PlaybackMode::mono, PlaybackMode::reverse,
                                  PlaybackMode::left, PlaybackMode::right};

class PlaybackSampleContract : public testing::Test {
protected:
  PlaybackSamples samples{[](size_t) { return size_t{1}; }};
  PcmEncoder encoder{[] { return int64_t{0}; }};
};

TEST_F(PlaybackSampleContract, Native16NormalizationPreservesExtremeStereoBytes) {
  ASSERT_TRUE(encoder.configure({SPS_FORMAT_S32_LE, 2}, 16));
  auto audio = *ConvertedAudio::allocate(400, 100, 0, {2, 16, 16});
  for (size_t frame = 0; frame < 100; ++frame) {
    const int16_t pair[]{INT16_MIN, INT16_MAX};
    std::memcpy(audio.bytes().data() + frame * 4, pair, 4);
  }
  ASSERT_TRUE(samples.prepare(audio, PlaybackMode::stereo));
  encoder.beginFrame(0x10000, false);
  auto encoded = samples.encode(encoder, {CorrectionStyle::basic, 0});
  EXPECT_EQ(encoded.frames(), 100);
  ASSERT_EQ(encoded.bytes().size(), 800);
  const uint8_t expected[]{0, 0, 0, 0x80, 0, 0, 0xff, 0x7f};
  EXPECT_EQ(std::memcmp(encoded.bytes().data(), expected, sizeof(expected)), 0);
}

TEST_F(PlaybackSampleContract, StereoModesPreserveNative16And32ChannelExpectations) {
  const std::array<std::array<int32_t, 2>, 5> expected16{{
    {INT32_MIN, 2147418112}, {-32768, -32768}, {2147418112, INT32_MIN},
    {INT32_MIN, INT32_MIN}, {2147418112, 2147418112}
  }};
  std::vector<int16_t> stereo16(200);
  std::vector<int32_t> stereo32(200);
  for (size_t frame = 0; frame < 100; ++frame) {
    stereo16[frame * 2] = INT16_MIN;
    stereo16[frame * 2 + 1] = INT16_MAX;
    stereo32[frame * 2] = -3;
    stereo32[frame * 2 + 1] = 2;
  }
  const std::array<std::array<int32_t, 2>, 5> expected32{{
    {-3, 2}, {-1, -1}, {2, -3}, {-3, -3}, {2, 2}
  }};
  for (size_t mode = 0; mode < modes.size(); ++mode) {
    SCOPED_TRACE(testing::Message() << "Playback mode " << static_cast<int>(modes[mode]));
    auto from16 = nativeAudio(stereo16, 2, 16);
    auto result16 = encode(samples, encoder, from16, modes[mode], {CorrectionStyle::basic, 0});
    EXPECT_EQ(sampleAt(result16, 0), expected16[mode][0]);
    EXPECT_EQ(sampleAt(result16, 1), expected16[mode][1]);
    auto from32 = nativeAudio(stereo32, 2, 32);
    auto result32 = encode(samples, encoder, from32, modes[mode], {CorrectionStyle::basic, 0});
    EXPECT_EQ(sampleAt(result32, 0), expected32[mode][0]);
    EXPECT_EQ(sampleAt(result32, 1), expected32[mode][1]);
  }
}

TEST_F(PlaybackSampleContract, MonoAndSurroundPayloadsKeepChannelOrderInEveryMode) {
  for (unsigned channels : {1U, 6U, 8U}) {
    SCOPED_TRACE(testing::Message() << "Channels " << channels);
    std::vector<int16_t> input(100 * channels);
    for (size_t index = 0; index < input.size(); ++index)
      input[index] = static_cast<int16_t>(int(index % channels) - 3);
    auto audio = nativeAudio(input, channels, 16);
    for (auto mode : modes) {
      SCOPED_TRACE(testing::Message() << "Playback mode " << static_cast<int>(mode));
      auto result = encode(samples, encoder, audio, mode, {CorrectionStyle::basic, 0});
      EXPECT_EQ(result.frames(), 100);
      for (unsigned channel = 0; channel < channels; ++channel)
        EXPECT_EQ(sampleAt(result, channel), int32_t(input[channel]) * 65536);
    }
  }
}

TEST_F(PlaybackSampleContract, EmptyPayloadClearsPreviouslyPreparedSamples) {
  auto valid = nativeAudio(std::vector<int16_t>(200, 1), 2, 16);
  ASSERT_TRUE(samples.prepare(valid, PlaybackMode::stereo));
  ASSERT_TRUE(encoder.configure({SPS_FORMAT_S32_LE, 2}, 16));
  ConvertedAudio empty;
  EXPECT_TRUE(samples.prepare(empty, PlaybackMode::stereo));
  EXPECT_TRUE(samples.encode(encoder, {CorrectionStyle::basic, 1}).bytes().empty());
}

TEST_F(PlaybackSampleContract, IncoherentPayloadClearsPreviouslyPreparedSamples) {
  auto valid = nativeAudio(std::vector<int16_t>(200, 1), 2, 16);
  ASSERT_TRUE(samples.prepare(valid, PlaybackMode::stereo));
  ASSERT_TRUE(encoder.configure({SPS_FORMAT_S32_LE, 2}, 16));
  auto incoherent = *ConvertedAudio::allocate(3, 100, 0, {2, 16, 16});
  EXPECT_FALSE(samples.prepare(incoherent, PlaybackMode::stereo));
  EXPECT_TRUE(samples.encode(encoder, {CorrectionStyle::basic, 1}).bytes().empty());
}

TEST_F(PlaybackSampleContract, BasicInteriorCorrectionAveragesEachChannelAndChoosesOnlyWhenNeeded) {
  std::vector<int32_t> ramp(200);
  for (size_t frame = 0; frame < 100; ++frame) {
    ramp[frame * 2] = static_cast<int32_t>(frame * 100);
    ramp[frame * 2 + 1] = static_cast<int32_t>(10000 + frame * 100);
  }
  auto rampAudio = nativeAudio(ramp, 2, 32);
  for (size_t selectedAt : {size_t{1}, size_t{98}}) {
    SCOPED_TRACE(testing::Message() << "Correction at frame " << selectedAt);
    unsigned choices = 0;
    PlaybackSamples selected([&](size_t frames) {
      EXPECT_EQ(frames, 100);
      ++choices;
      return selectedAt;
    });
    auto inserted = encode(selected, encoder, rampAudio, PlaybackMode::stereo,
                            {CorrectionStyle::basic, 1});
    EXPECT_EQ(inserted.frames(), 101);
    const int32_t expectedMean = selectedAt == 1 ? 50 : 9750;
    EXPECT_EQ(sampleAt(inserted, selectedAt * 2), expectedMean);
    EXPECT_EQ(sampleAt(inserted, selectedAt * 2 + 1), expectedMean + 10000);
    EXPECT_EQ(sampleAt(inserted, 0), 0);
    EXPECT_EQ(sampleAt(inserted, 200), 9900);
    auto removed = encode(selected, encoder, rampAudio, PlaybackMode::stereo,
                           {CorrectionStyle::basic, -1});
    EXPECT_EQ(removed.frames(), 99);
    EXPECT_EQ(sampleAt(removed, 196), 9900);
    encode(selected, encoder, rampAudio, PlaybackMode::stereo, {CorrectionStyle::basic, 0});
    EXPECT_EQ(choices, 2);
  }
}

TEST_F(PlaybackSampleContract, VernierCorrectionRespectsMinimumLengthAndRequestedCounts) {
  for (size_t frames : {size_t{99}, size_t{100}}) {
    auto constant = nativeAudio(std::vector<int32_t>(frames * 2, 1000), 2, 32);
    for (int delta : {-20, -1, 0, 1, 20}) {
      SCOPED_TRACE(testing::Message() << frames << " frames, correction " << delta);
      auto corrected = encode(samples, encoder, constant, PlaybackMode::stereo,
                               {CorrectionStyle::vernier, delta});
      EXPECT_EQ(corrected.frames(), static_cast<size_t>(int(frames) + (frames == 99 ? 0 : delta)));
      EXPECT_EQ(sampleAt(corrected, 0), 1000);
      EXPECT_EQ(sampleAt(corrected, corrected.frames() * 2 - 1), 1000);
    }
  }
}

TEST_F(PlaybackSampleContract, EncodedHandoffPreservesStereoBytesAndBoundsBasicCorrection) {
  std::array<int32_t, 512> playback;
  PlaybackSamples handoff([](size_t) { return size_t{1}; });
  const auto encodeCorrection = [&](bool interpolate, int delta) {
    auto native = nativeAudio(std::vector<int32_t>(playback.begin(), playback.end()), 2, 16);
    const auto prepared = handoff.prepare(native, PlaybackMode::stereo);
    EXPECT_TRUE(prepared);
    if (!prepared)
      return EncodedPcm{};
    return handoff.encode(encoder, {interpolate ? CorrectionStyle::vernier : CorrectionStyle::basic, delta});
  };
  playback.fill(0x12340000);
  ASSERT_TRUE(encoder.configure({SPS_FORMAT_S16_LE, 2}, 16));
  for (bool interpolate : {false, true})
    for (int adjustment : {-1, 0, 1}) {
      encoder.beginFrame(0x10000, false);
      auto output = encodeCorrection(interpolate, adjustment);
      EXPECT_EQ(output.frames(), static_cast<size_t>(256 + adjustment));
      ASSERT_EQ(output.bytes().size(), output.frames() * 4);
      ASSERT_EQ(output.bytes().size() % 2, 0);
      for (size_t offset = 0; offset < output.bytes().size(); offset += 2) {
        EXPECT_EQ(output.bytes()[offset], 0x34);
        EXPECT_EQ(output.bytes()[offset + 1], 0x12);
      }
    }

  for (size_t frame = 0; frame < 256; ++frame) {
    playback[frame * 2] = 1000 * 65536;
    playback[frame * 2 + 1] = 2000 * 65536;
  }
  encoder.beginFrame(0x10000, false);
  auto inserted = encodeCorrection(false, 1);
  EXPECT_EQ(inserted.frames(), 257);
  ASSERT_EQ(inserted.bytes().size() % 4, 0);
  for (size_t offset = 0; offset < inserted.bytes().size(); offset += 4) {
    EXPECT_EQ(inserted.bytes()[offset], 0xe8);
    EXPECT_EQ(inserted.bytes()[offset + 1], 0x03);
    EXPECT_EQ(inserted.bytes()[offset + 2], 0xd0);
    EXPECT_EQ(inserted.bytes()[offset + 3], 0x07);
  }
  for (int requested : {-3, 3}) {
    encoder.beginFrame(0x10000, false);
    auto bounded = encodeCorrection(false, requested);
    EXPECT_EQ(bounded.frames(), static_cast<size_t>(256 + (requested > 0 ? 1 : -1)));
    ASSERT_GE(bounded.bytes().size(), 4);
    EXPECT_EQ(bounded.bytes()[0], 0xe8);
    EXPECT_EQ(bounded.bytes()[2], 0xd0);
    const auto last = bounded.bytes().size() - 4;
    EXPECT_EQ(bounded.bytes()[last], 0xe8);
    EXPECT_EQ(bounded.bytes()[last + 2], 0xd0);
  }
}
