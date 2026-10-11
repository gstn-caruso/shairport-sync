module;
#include <cstdint>
#include <algorithm>
#include <array>
#include <expected>
#include <span>
#include <vector>
#include <sodium.h>
module receiver.protocol.ap2.buffered_block;

namespace {
std::uint32_t bigEndian(std::span<const std::uint8_t> bytes) {
  return (std::uint32_t{bytes[0]} << 24) | (std::uint32_t{bytes[1]} << 16) |
         (std::uint32_t{bytes[2]} << 8) | bytes[3];
}
}
std::expected<BufferedAudioBlock, BufferedBlockError>
BufferedAudioBlock::parse(std::span<const std::uint8_t> wire) {
  if (wire.size() < minimumSize || wire.size() > maximumSize)
    return std::unexpected(BufferedBlockError::invalidSize);
  return BufferedAudioBlock(wire, bigEndian(wire.first(4)) & 0x7fffff,
      bigEndian(wire.subspan(4, 4)), bigEndian(wire.subspan(8, 4)));
}
std::expected<std::vector<std::uint8_t>, BufferedBlockError>
BufferedAudioBlock::prepare(BufferedBlockFormat format, std::span<const std::uint8_t> key,
                            unsigned inputRate) const {
  if (key.empty())
    return std::unexpected(BufferedBlockError::missingKey);
  if (key.size() != crypto_aead_chacha20poly1305_ietf_KEYBYTES)
    return std::unexpected(BufferedBlockError::invalidKeySize);
  std::array<std::uint8_t, 12> nonce{};
  std::copy(wire_.end() - 8, wire_.end(), nonce.begin() + 4);
  const std::size_t leader = format.codec == BufferedBlockCodec::aac ? 7 : 0;
  std::vector<std::uint8_t> payload(leader + wire_.size() - minimumSize);
  unsigned long long written = 0;
  if (crypto_aead_chacha20poly1305_ietf_decrypt(payload.data() + leader, &written, nullptr,
      wire_.data() + 12, wire_.size() - 20, wire_.data() + 4, 8, nonce.data(), key.data()) != 0)
    return std::unexpected(BufferedBlockError::authenticationFailed);
  if (written == 0)
    return std::unexpected(BufferedBlockError::emptyPlaintext);
  payload.resize(leader + written);
  if (format.codec == BufferedBlockCodec::aac) {
    const unsigned frequencyIndex = inputRate == 48000 ? 3 : 4;
    const auto channels = format.aacChannelConfiguration;
    const auto size = payload.size();
    payload[0] = 0xff;
    payload[1] = 0xf9;
    payload[2] = (1 << 6) + (frequencyIndex << 2) + (channels >> 2);
    payload[3] = ((channels & 3) << 6) + (size >> 11);
    payload[4] = (size & 0x7ff) >> 3;
    payload[5] = ((size & 7) << 5) + 0x1f;
    payload[6] = 0xfc;
  }
  return payload;
}
