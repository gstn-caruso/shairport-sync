module;
#include <cstdint>
#include <expected>
#include <span>
#include <vector>
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
BufferedAudioBlock::prepare(BufferedBlockFormat, std::span<const std::uint8_t>, unsigned) const {
  return std::unexpected(BufferedBlockError::authenticationFailed);
}
