#include "audio_input_state.hpp"
#include <gtest/gtest.h>

TEST(AudioInputState, PacketShapeDoesNotClaimDecoderValidity) {
  AudioInputState input;
  EXPECT_EQ(input.sampleRate(), 0);
  EXPECT_EQ(input.framesPerPacket(), 0);
  EXPECT_FALSE(input.isDecodedFormatValid());

  const auto format = AudioFormat::fromSsrc(ALAC_44100_S16_2);
  ASSERT_TRUE(format);
  input.recordPacketShape(*format);

  EXPECT_EQ(input.sampleRate(), 44100);
  EXPECT_EQ(input.framesPerPacket(), 352);
  EXPECT_FALSE(input.isDecodedFormatValid());
}

TEST(AudioInputState, DecodedFormatRecordsShapeAndValidity) {
  AudioInputState input;
  const auto packetFormat = AudioFormat::fromSsrc(ALAC_44100_S16_2);
  ASSERT_TRUE(packetFormat);
  input.recordPacketShape(*packetFormat);
  EXPECT_EQ(input.sampleRate(), 44100);
  EXPECT_EQ(input.framesPerPacket(), 352);
  EXPECT_FALSE(input.isDecodedFormatValid());

  const auto decodedFormat = AudioFormat::fromSsrc(AAC_48000_F24_2);
  ASSERT_TRUE(decodedFormat);
  input.recordDecodedFormat(*decodedFormat);

  EXPECT_EQ(input.sampleRate(), 48000);
  EXPECT_EQ(input.framesPerPacket(), 1024);
  EXPECT_TRUE(input.isDecodedFormatValid());
}

TEST(AudioInputState, SetupOverridesPreserveOtherShapeAndValidity) {
  AudioInputState input;
  const auto decodedFormat = AudioFormat::fromSsrc(AAC_48000_F24_2);
  ASSERT_TRUE(decodedFormat);
  input.recordDecodedFormat(*decodedFormat);
  EXPECT_EQ(input.sampleRate(), 48000);
  EXPECT_EQ(input.framesPerPacket(), 1024);
  EXPECT_TRUE(input.isDecodedFormatValid());

  input.setSetupSampleRate(44100);
  EXPECT_EQ(input.sampleRate(), 44100);
  EXPECT_EQ(input.framesPerPacket(), 1024);
  EXPECT_TRUE(input.isDecodedFormatValid());

  input.setSetupPacketFrames(352);
  EXPECT_EQ(input.sampleRate(), 44100);
  EXPECT_EQ(input.framesPerPacket(), 352);
  EXPECT_TRUE(input.isDecodedFormatValid());
}
