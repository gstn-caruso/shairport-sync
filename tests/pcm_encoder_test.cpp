#include "audio/pcm/pcm_encoder.hpp"
#include <array>
#include <gtest/gtest.h>
#include <cstring>
#include <vector>

static void checkStereoFrame(PcmEncoder &encoder) {
  ASSERT_TRUE(encoder.configure({SPS_FORMAT_S16_LE, 2}, 16));
  encoder.beginFrame(0x10000, false);
  encoder.appendSample(0x12345678);
  encoder.appendSample(-0x12345678);
  auto encoded = encoder.finishFrame();
  EXPECT_EQ(encoded.frames(), 1);
  const std::array<uint8_t, 4> expected{0x34, 0x12, 0xcb, 0xed};
  EXPECT_TRUE(std::equal(encoded.bytes().begin(), encoded.bytes().end(), expected.begin(), expected.end()));
}

static void checkWireFormatsThrough(PcmEncoder &encoder, sps_format_t lastFormat) {
  ASSERT_NO_FATAL_FAILURE(checkStereoFrame(encoder));
  struct Case { sps_format_t format; std::vector<uint8_t> positive, negative; };
  const std::array cases{
    Case{SPS_FORMAT_S8, {0x12}, {0xed}},
    Case{SPS_FORMAT_U8, {0x92}, {0x6d}},
    Case{SPS_FORMAT_S16_LE, {0x34,0x12}, {0xcb,0xed}},
    Case{SPS_FORMAT_S16_BE, {0x12,0x34}, {0xed,0xcb}},
    Case{SPS_FORMAT_S24_3LE, {0x56,0x34,0x12}, {0xa9,0xcb,0xed}},
    Case{SPS_FORMAT_S24_3BE, {0x12,0x34,0x56}, {0xed,0xcb,0xa9}},
    Case{SPS_FORMAT_S24_LE, {0x56,0x34,0x12,0}, {0xa9,0xcb,0xed,0}},
    Case{SPS_FORMAT_S24_BE, {0,0x12,0x34,0x56}, {0,0xed,0xcb,0xa9}},
    Case{SPS_FORMAT_S32_LE, {0x78,0x56,0x34,0x12}, {0x88,0xa9,0xcb,0xed}},
    Case{SPS_FORMAT_S32_BE, {0x12,0x34,0x56,0x78}, {0xed,0xcb,0xa9,0x88}}
  };
  for (const auto &test : cases) {
    for (bool negative : {false, true}) {
      const auto &expected = negative ? test.negative : test.positive;
      ASSERT_TRUE(encoder.configure({test.format, 1}, 0));
      encoder.beginFrame(0x10000, false);
      encoder.appendSample(negative ? -0x12345678 : 0x12345678);
      auto owned = encoder.finishFrame();
      EXPECT_EQ(owned.frames(), 1);
      EXPECT_TRUE(std::equal(owned.bytes().begin(), owned.bytes().end(),
                             expected.begin(), expected.end()));
    }
    ASSERT_TRUE(encoder.configure({test.format, 2}, 0));
    auto silence = encoder.silence(1);
    EXPECT_EQ(silence.frames(), 1);
    for (auto byte : silence.bytes())
      EXPECT_EQ(byte, test.format == SPS_FORMAT_U8 ? 128 : 0);
    if (test.format == lastFormat)
      return;
  }
}

static void checkNativeFormatsThrough(PcmEncoder &encoder, sps_format_t lastFormat) {
  ASSERT_NO_FATAL_FAILURE(checkWireFormatsThrough(encoder, SPS_FORMAT_S32_BE));
  for (auto format : {SPS_FORMAT_S16, SPS_FORMAT_S24, SPS_FORMAT_S32}) {
    ASSERT_TRUE(encoder.configure({format, 1}, 0));
    encoder.beginFrame(0x10000, false);
    encoder.appendSample(-0x12345678);
    auto bytes = encoder.finishFrame();
    if (format == SPS_FORMAT_S16) {
      int16_t value;
      ASSERT_GE(bytes.bytes().size(), sizeof(value));
      std::memcpy(&value, bytes.bytes().data(), sizeof(value));
      EXPECT_EQ(value, -0x1235);
    } else {
      int32_t value;
      ASSERT_GE(bytes.bytes().size(), sizeof(value));
      std::memcpy(&value, bytes.bytes().data(), sizeof(value));
      EXPECT_EQ(value, format == SPS_FORMAT_S24 ? -0x123457 : -0x12345678);
    }
    if (format == lastFormat)
      return;
  }
}

static void checkDitherClippingAndSeedContinuity(PcmEncoder &deterministic, int &calls) {
  ASSERT_TRUE(deterministic.configure({SPS_FORMAT_S16_LE, 1}, 32));
  deterministic.beginFrame(0x10000, false);
  EXPECT_TRUE(deterministic.dithers());
  deterministic.appendSample(INT32_MAX);
  deterministic.appendSample(INT32_MIN);
  auto extremes = deterministic.finishFrame();
  const std::array<uint8_t, 4> clipped{0xff, 0x7f, 0, 0x80};
  EXPECT_TRUE(std::equal(extremes.bytes().begin(), extremes.bytes().end(), clipped.begin(), clipped.end()));
  auto betweenFrames = deterministic.silence(1);
  ASSERT_GE(betweenFrames.bytes().size(), 2);
  EXPECT_EQ(betweenFrames.bytes()[0], 0);
  EXPECT_EQ(betweenFrames.bytes()[1], 0);
  deterministic.beginFrame(0x10000, false);
  deterministic.appendSample(0);
  auto afterSilence = deterministic.finishFrame();
  ASSERT_GE(afterSilence.bytes().size(), 2);
  EXPECT_EQ(afterSilence.bytes()[0], 0xff);
  EXPECT_EQ(afterSilence.bytes()[1], 0xff);
  EXPECT_EQ(calls, 4);
}

TEST(PcmEncoder, StereoFrameEncodesBothSignedSamplesInLittleEndianOrder) {
  PcmEncoder encoder([] { return int64_t{0}; });
  checkStereoFrame(encoder);
}

TEST(PcmEncoder, Signed8BitEncodesPositiveNegativeAndSilence) {
  PcmEncoder encoder([] { return int64_t{0}; });
  checkWireFormatsThrough(encoder, SPS_FORMAT_S8);
}

TEST(PcmEncoder, Unsigned8BitEncodesPositiveNegativeAndBiasedSilence) {
  PcmEncoder encoder([] { return int64_t{0}; });
  checkWireFormatsThrough(encoder, SPS_FORMAT_U8);
}

TEST(PcmEncoder, Signed16LittleEndianEncodesPositiveNegativeAndSilence) {
  PcmEncoder encoder([] { return int64_t{0}; });
  checkWireFormatsThrough(encoder, SPS_FORMAT_S16_LE);
}

TEST(PcmEncoder, Signed16BigEndianEncodesPositiveNegativeAndSilence) {
  PcmEncoder encoder([] { return int64_t{0}; });
  checkWireFormatsThrough(encoder, SPS_FORMAT_S16_BE);
}

TEST(PcmEncoder, Packed24LittleEndianEncodesPositiveNegativeAndSilence) {
  PcmEncoder encoder([] { return int64_t{0}; });
  checkWireFormatsThrough(encoder, SPS_FORMAT_S24_3LE);
}

TEST(PcmEncoder, Packed24BigEndianEncodesPositiveNegativeAndSilence) {
  PcmEncoder encoder([] { return int64_t{0}; });
  checkWireFormatsThrough(encoder, SPS_FORMAT_S24_3BE);
}

TEST(PcmEncoder, Padded24LittleEndianEncodesPositiveNegativeAndSilence) {
  PcmEncoder encoder([] { return int64_t{0}; });
  checkWireFormatsThrough(encoder, SPS_FORMAT_S24_LE);
}

TEST(PcmEncoder, Padded24BigEndianEncodesPositiveNegativeAndSilence) {
  PcmEncoder encoder([] { return int64_t{0}; });
  checkWireFormatsThrough(encoder, SPS_FORMAT_S24_BE);
}

TEST(PcmEncoder, Signed32LittleEndianEncodesPositiveNegativeAndSilence) {
  PcmEncoder encoder([] { return int64_t{0}; });
  checkWireFormatsThrough(encoder, SPS_FORMAT_S32_LE);
}

TEST(PcmEncoder, Signed32BigEndianEncodesPositiveNegativeAndSilence) {
  PcmEncoder encoder([] { return int64_t{0}; });
  checkWireFormatsThrough(encoder, SPS_FORMAT_S32_BE);
}

TEST(PcmEncoder, Native16PreservesSignedSampleValue) {
  PcmEncoder encoder([] { return int64_t{0}; });
  checkNativeFormatsThrough(encoder, SPS_FORMAT_S16);
}

TEST(PcmEncoder, Native24SignExtendsNegativeSample) {
  PcmEncoder encoder([] { return int64_t{0}; });
  checkNativeFormatsThrough(encoder, SPS_FORMAT_S24);
}

TEST(PcmEncoder, Native32PreservesSignedSampleValue) {
  PcmEncoder encoder([] { return int64_t{0}; });
  checkNativeFormatsThrough(encoder, SPS_FORMAT_S32);
}

TEST(PcmEncoder, DitherClipsExtremesAndSilencePreservesSeedContinuity) {
  int calls = 0;
  PcmEncoder deterministic([&] {
    return (++calls % 2) ? (int64_t{1} << 48) - 1 : int64_t{0};
  });
  checkDitherClippingAndSeedContinuity(deterministic, calls);
}

TEST(PcmEncoder, DitherPolicyTracksReconfigurationGainAndSilence) {
  int calls = 0;
  PcmEncoder deterministic([&] {
    return (++calls % 2) ? (int64_t{1} << 48) - 1 : int64_t{0};
  });
  ASSERT_NO_FATAL_FAILURE(checkDitherClippingAndSeedContinuity(deterministic, calls));
  ASSERT_TRUE(deterministic.configure({SPS_FORMAT_S16_LE, 1}, 16));
  deterministic.beginFrame(0x10000, false);
  EXPECT_FALSE(deterministic.dithers());
  deterministic.appendSample(0);
  EXPECT_EQ(calls, 4);
  deterministic.finishFrame();
  deterministic.silence(1, DitherPolicy::disabled);
  EXPECT_EQ(calls, 5);
  deterministic.beginFrame(0x8000, false);
  EXPECT_TRUE(deterministic.dithers());
  deterministic.beginFrame(0x10000, true);
  EXPECT_TRUE(deterministic.dithers());
}

TEST(PcmEncoder, FixedGainAttenuatesAfterWireAndNativeFormatChanges) {
  PcmEncoder encoder([] { return int64_t{0}; });
  ASSERT_NO_FATAL_FAILURE(checkNativeFormatsThrough(encoder, SPS_FORMAT_S32));
  ASSERT_TRUE(encoder.configure({SPS_FORMAT_S16_LE, 1}, 16));
  encoder.beginFrame(0x8000, false);
  encoder.appendSample(0x12345678);
  auto attenuated = encoder.finishFrame();
  ASSERT_GE(attenuated.bytes().size(), 2);
  EXPECT_EQ(attenuated.bytes()[0], 0x1a);
  EXPECT_EQ(attenuated.bytes()[1], 0x09);
}
