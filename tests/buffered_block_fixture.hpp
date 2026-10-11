#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>
#include <sodium.h>

namespace buffered_block_fixture {
inline constexpr std::array<std::uint8_t, 32> key{
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
inline constexpr std::array<std::uint8_t, 16> plaintext{
    0x10,0x21,0x32,0x43,0x54,0x65,0x76,0x87,0x98,0xa9,0xba,0xcb,0xdc,0xed,0xfe,0x0f};
inline constexpr std::array<std::uint8_t, 52> golden{
    0x80,0xab,0xcd,0xef,1,2,3,4,0x16,0,0,0,
    0xff,0xc7,0x97,0xbb,0xf1,0xe9,0xde,0x1b,0x88,0x16,0x52,0x1d,0x56,0x01,0xd9,0x51,
    0x58,0x5d,0x00,0xf5,0x10,0x34,0x7f,0xf9,0x4d,0xc2,0x09,0x09,0x92,0x18,0xe9,0xc1,
    1,2,3,4,5,6,7,8};
inline std::vector<std::uint8_t> encrypt(std::span<const std::uint8_t> payload = plaintext,
                                        std::uint32_t ssrc = 0x16000000,
                                        std::uint32_t timestamp = 0x01020304) {
  std::vector<std::uint8_t> block{0x80,0xab,0xcd,0xef,1,2,3,4,0x16,0,0,0};
  for (unsigned offset = 0; offset < 4; ++offset)
    block[8 + offset] = ssrc >> (24 - 8 * offset);
  for (unsigned offset = 0; offset < 4; ++offset)
    block[4 + offset] = timestamp >> (24 - 8 * offset);
  const std::array<std::uint8_t, 12> nonce{0,0,0,0,1,2,3,4,5,6,7,8};
  block.resize(12 + payload.size() + 16 + 8);
  unsigned long long written = 0;
  if (crypto_aead_chacha20poly1305_ietf_encrypt(block.data() + 12, &written,
      payload.data(), payload.size(), block.data() + 4, 8, nullptr, nonce.data(), key.data()) != 0)
    throw std::runtime_error("Fixture encryption failed");
  std::copy(nonce.begin() + 4, nonce.end(), block.end() - 8);
  return block;
}
}
