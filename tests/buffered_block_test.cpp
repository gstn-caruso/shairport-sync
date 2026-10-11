#include "buffered_block_fixture.hpp"
#include <gtest/gtest.h>
import receiver.protocol.ap2.buffered_block;

TEST(BufferedAudioBlock, CopiesBigEndianMetadataAndMasksSequenceToTwentyThreeBits) {
  auto parsed = BufferedAudioBlock::parse(buffered_block_fixture::golden);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->sequence(), 0x2bcdefu);
  EXPECT_EQ(parsed->timestamp(), 0x01020304u);
  EXPECT_EQ(parsed->ssrc(), 0x16000000u);
}
