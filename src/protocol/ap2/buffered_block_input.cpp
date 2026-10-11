#include "protocol/ap2/buffered_block_input.hpp"
#include <array>
import receiver.protocol.ap2.buffered_block;

namespace {
BufferedBlockReadStatus terminalStatus(ByteQueueStatus status) {
  switch (status) {
  case ByteQueueStatus::complete: return BufferedBlockReadStatus::complete;
  case ByteQueueStatus::endOfStream: return BufferedBlockReadStatus::channelClosed;
  case ByteQueueStatus::error: return BufferedBlockReadStatus::readError;
  case ByteQueueStatus::stopped: return BufferedBlockReadStatus::stopped;
  }
  std::terminate();
}
}
BufferedBlockRead readBufferedAudioBlock(ExactByteInput &input, std::span<std::uint8_t> storage) {
  BufferedBlockRead result{BufferedBlockReadStatus::complete};
  std::array<std::uint8_t,2> prefix{};
  auto read = input.readExact(prefix);
  result.prefixRemaining = read.remaining;
  result.declaredLength = (std::uint16_t{prefix[0]} << 8) | prefix[1];
  result.status = terminalStatus(read.status);
  result.errorCode = read.errorCode;
  if (read.status != ByteQueueStatus::complete) {
    if (read.status == ByteQueueStatus::endOfStream && read.count != 0)
      result.status = BufferedBlockReadStatus::invalidSize;
    return result;
  }
  if (read.count != prefix.size() || result.declaredLength < prefix.size()) {
    result.status = BufferedBlockReadStatus::invalidSize;
    return result;
  }
  const std::size_t bodyLength = result.declaredLength - prefix.size();
  if (bodyLength < BufferedAudioBlock::minimumSize || bodyLength > BufferedAudioBlock::maximumSize ||
      bodyLength > storage.size()) {
    result.status = BufferedBlockReadStatus::invalidSize;
    return result;
  }
  read = input.readExact(storage.first(bodyLength));
  result.bodyRemaining = read.remaining;
  result.count = read.count;
  result.status = terminalStatus(read.status);
  result.errorCode = read.errorCode;
  return result;
}
