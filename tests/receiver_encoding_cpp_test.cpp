#include "runtime/common.h"
#include "playback/player.h"
#include <gtest/gtest.h>

constexpr sps_format_t unsupported_format = static_cast<sps_format_t>(63);
constexpr ssrc_t unsupported_stream = static_cast<ssrc_t>(UINT32_MAX);

static_assert(sizeof(ssrc_t) == sizeof(uint32_t));
static_assert(sizeof(sps_format_t) == sizeof(uint32_t));

TEST(ReceiverEncoding, UnsupportedEncodedFormatRemainsRepresentableButInvalid) {
  EXPECT_EQ(FORMAT_FROM_ENCODED_FORMAT(63), unsupported_format);
  EXPECT_EQ(sps_format_sample_size(unsupported_format), 0);
  EXPECT_STREQ(sps_format_description_string(unsupported_format), "invalid");
}

TEST(ReceiverEncoding, Signed16LittleEndianUsesTwoByteSamples) {
  EXPECT_EQ(sps_format_sample_size(FORMAT_FROM_ENCODED_FORMAT(SPS_FORMAT_S16_LE)), 2);
}

TEST(ReceiverEncoding, UnknownWireSsrcIsUnrecognised) {
  EXPECT_EQ(ssrc_is_recognised(unsupported_stream), 0);
}
