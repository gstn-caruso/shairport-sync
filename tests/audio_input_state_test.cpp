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
