#include "protocol/ap2/buffered_block_input.hpp"
#include <arpa/inet.h>

BufferedBlockRead readBufferedAudioBlock(buffered_tcp_desc &input, std::span<std::uint8_t> storage) {
  BufferedBlockRead result{BufferedBlockReadStatus::complete};
  std::uint16_t length = 0;
  auto count = read_sized_block(&input, &length, sizeof(length), &result.prefixRemaining);
  result.declaredLength = ntohs(length);
  if (count > 0) {
    std::size_t remaining = 0;
    count = read_sized_block(&input, storage.data(), result.declaredLength - 2, &remaining);
    result.bodyRemaining = remaining;
  }
  if (count > 0)
    result.count = count;
  else
    result.status = count == 0 ? BufferedBlockReadStatus::channelClosed : BufferedBlockReadStatus::readError;
  return result;
}
