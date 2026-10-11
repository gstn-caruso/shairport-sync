#pragma once
#include "platform/utilities/buffered_read.h"
#include <cstdint>
#include <optional>
#include <span>

enum class BufferedBlockReadStatus { complete, channelClosed, readError, invalidSize };
struct BufferedBlockRead {
  BufferedBlockReadStatus status;
  std::size_t count = 0;
  std::uint16_t declaredLength = 0;
  std::size_t prefixRemaining = 0;
  std::optional<std::size_t> bodyRemaining{};
};
BufferedBlockRead readBufferedAudioBlock(buffered_tcp_desc &input, std::span<std::uint8_t> storage);
