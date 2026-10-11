#pragma once
#include "transport/exact_byte_input.hpp"
#include <cstdint>
#include <optional>
#include <span>

enum class BufferedBlockReadStatus { complete, channelClosed, readError, invalidSize, stopped };
struct BufferedBlockRead {
  BufferedBlockReadStatus status;
  std::size_t count = 0;
  std::uint16_t declaredLength = 0;
  std::size_t prefixRemaining = 0;
  std::optional<std::size_t> bodyRemaining{};
  int errorCode = 0;
};
BufferedBlockRead readBufferedAudioBlock(ExactByteInput &input, std::span<std::uint8_t> storage);
