#include "buffered_block_fixture.hpp"
#include <gtest/gtest.h>
#include <span>

import receiver.protocol.ap2.buffered_block;

TEST(BufferedBlockLegacy, SodiumContractAndAdtsMatchDeterministicGoldenBytes) {
  ASSERT_GE(sodium_init(), 0);
  auto wire = buffered_block_fixture::encrypt();
  EXPECT_EQ(wire, std::vector<uint8_t>(buffered_block_fixture::golden.begin(), buffered_block_fixture::golden.end()));
  std::array<uint8_t, 12> nonce{};
  std::copy(wire.end() - 8, wire.end(), nonce.begin() + 4);
  std::array<uint8_t, 32> payload{};
  unsigned long long length = 0;
  ASSERT_EQ(crypto_aead_chacha20poly1305_ietf_decrypt(payload.data(), &length, nullptr,
      wire.data() + 12, wire.size() - 20, wire.data() + 4, 8, nonce.data(),
      buffered_block_fixture::key.data()), 0);
  EXPECT_EQ(length, buffered_block_fixture::plaintext.size());
  EXPECT_TRUE(std::equal(payload.begin(), payload.begin() + length,
      buffered_block_fixture::plaintext.begin(), buffered_block_fixture::plaintext.end()));
  std::array<uint8_t, 7> adts{};
  auto block = BufferedAudioBlock::parse(wire);
  ASSERT_TRUE(block);
  auto framed = block->prepare({BufferedBlockCodec::aac, 2}, buffered_block_fixture::key, 44100);
  ASSERT_TRUE(framed);
  std::copy_n(framed->begin(), 7, adts.begin());
  EXPECT_EQ(adts, (std::array<uint8_t,7>{0xff,0xf9,0x50,0x80,0x02,0xff,0xfc}));
}
