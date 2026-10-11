#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

enum class ByteQueueStatus { complete, endOfStream, error, stopped };
struct ByteQueueResult {
  ByteQueueStatus status;
  std::size_t count;
  std::size_t remaining;
  int errorCode = 0;
};
class ExactByteInput {
public:
  virtual ~ExactByteInput() = default;
  virtual ByteQueueResult readExact(std::span<std::uint8_t> destination) = 0;
};
