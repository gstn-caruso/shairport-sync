#include "protocol/ap2/buffered_block_input.hpp"
#include <arpa/inet.h>
import receiver.protocol.ap2.buffered_block;

BufferedBlockRead readBufferedAudioBlock(buffered_tcp_desc &input, std::span<std::uint8_t> storage) {
  BufferedBlockRead result{BufferedBlockReadStatus::complete};
  std::uint16_t length = 0;
  auto count = read_sized_block(&input, &length, sizeof(length), &result.prefixRemaining);
  result.declaredLength = ntohs(length);
  if (count > 0) {
    if (count != sizeof(length) || result.declaredLength < sizeof(length)) {
      result.status = BufferedBlockReadStatus::invalidSize;
      return result;
    }
    const std::size_t bodyLength = result.declaredLength - sizeof(length);
    if (bodyLength < BufferedAudioBlock::minimumSize || bodyLength > BufferedAudioBlock::maximumSize ||
        bodyLength > storage.size()) {
      result.status = BufferedBlockReadStatus::invalidSize;
      return result;
    }
    std::size_t remaining = 0;
    count = read_sized_block(&input, storage.data(), bodyLength, &remaining);
    result.bodyRemaining = remaining;
  }
  if (count > 0)
    result.count = count;
  else
    result.status = count == 0 ? BufferedBlockReadStatus::channelClosed : BufferedBlockReadStatus::readError;
  return result;
}
