#include "buffered_block_fixture.hpp"
#include <gtest/gtest.h>
#include <span>
#include <cstdint>
import receiver.protocol.ap2.realtime_audio;

TEST(RealtimeEncryptedAudio, GoldenStrippedHeaderAuthenticatesMetadataAndOwnsPlaintext) {
  auto wire = buffered_block_fixture::encrypt();
  auto result = RealtimeEncryptedAudio::decode(std::span(wire).subspan(2),buffered_block_fixture::key);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->sequence,0xcdef);
  EXPECT_EQ(result->timestamp,0x01020304u);
  std::fill(wire.begin(),wire.end(),0);
  EXPECT_EQ(result->plaintext,(std::vector<uint8_t>(buffered_block_fixture::plaintext.begin(),buffered_block_fixture::plaintext.end())));
}
