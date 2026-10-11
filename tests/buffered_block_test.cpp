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

TEST(BufferedAudioBlock, GoldenAlacPayloadAuthenticatesAndOwnsPreparedBytes) {
  auto wire = buffered_block_fixture::encrypt();
  auto block = BufferedAudioBlock::parse(wire);
  ASSERT_TRUE(block);
  auto payload = block->prepare({BufferedBlockCodec::alac}, buffered_block_fixture::key, 44100);
  ASSERT_TRUE(payload);
  EXPECT_EQ(*payload, std::vector<uint8_t>(buffered_block_fixture::plaintext.begin(), buffered_block_fixture::plaintext.end()));
  std::fill(wire.begin(), wire.end(), 0);
  EXPECT_EQ(block->timestamp(), 0x01020304u);
  EXPECT_EQ((*payload)[0], 0x10);
  EXPECT_EQ((*payload)[15], 0x0f);
}
