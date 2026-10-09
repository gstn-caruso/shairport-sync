#include "audio_format.hpp"
#include <gtest/gtest.h>

struct FormatExpectation {
  ssrc_t ssrc;
  unsigned rate;
  size_t frames;
  bool aac;
  const char *name;
  unsigned channels;
  unsigned aacChannelConfiguration;
  sps_format_t sampleFormat;
};

static void checkFormat(const FormatExpectation &format) {
  auto actual = AudioFormat::fromSsrc(format.ssrc);
  ASSERT_TRUE(actual);
  EXPECT_EQ(actual->isAac(), format.aac);
  EXPECT_EQ(actual->sampleRate(), format.rate);
  EXPECT_EQ(actual->framesPerPacket(), format.frames);
  EXPECT_EQ(actual->name(), format.name);
  EXPECT_EQ(actual->ssrc(), format.ssrc);
  EXPECT_EQ(actual->channels(), format.channels);
  EXPECT_EQ(actual->aacChannelConfiguration(), format.aacChannelConfiguration);
  EXPECT_EQ(actual->suggestedSampleFormat(), format.sampleFormat);
}

TEST(AudioFormat, Alac44100StereoSuggests16BitPcm) {
  checkFormat({ALAC_44100_S16_2, 44100, 352, false, "ALAC/44100/S16_LE/2", 2, 2, SPS_FORMAT_S16});
}

TEST(AudioFormat, Alac48000StereoSuggests24BitPcm) {
  checkFormat({ALAC_48000_S24_2, 48000, 352, false, "ALAC/48000/S24_LE/2", 2, 2, SPS_FORMAT_S24});
}

TEST(AudioFormat, Aac44100StereoSuggests32BitPcm) {
  checkFormat({AAC_44100_F24_2, 44100, 1024, true, "AAC/44100/F24/2", 2, 2, SPS_FORMAT_S32});
}

TEST(AudioFormat, Aac48000StereoSuggests32BitPcm) {
  checkFormat({AAC_48000_F24_2, 48000, 1024, true, "AAC/48000/F24/2", 2, 2, SPS_FORMAT_S32});
}

TEST(AudioFormat, Aac48000Surround51UsesSixChannels) {
  checkFormat({AAC_48000_F24_5P1, 48000, 1024, true, "AAC/48000/F24/5.1", 6, 6, SPS_FORMAT_S32});
}

TEST(AudioFormat, Aac48000Surround71UsesChannelConfigurationSeven) {
  checkFormat({AAC_48000_F24_7P1, 48000, 1024, true, "AAC/48000/F24/7.1", 8, 7, SPS_FORMAT_S32});
}

TEST(AudioFormat, NoneSsrcIsRejected) {
  EXPECT_FALSE(AudioFormat::fromSsrc(SSRC_NONE));
}

TEST(AudioFormat, UnknownSsrcIsRejected) {
  EXPECT_FALSE(AudioFormat::fromSsrc(static_cast<ssrc_t>(0xf00d)));
}
