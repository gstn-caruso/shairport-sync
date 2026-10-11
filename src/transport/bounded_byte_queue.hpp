#pragma once
#include "transport/exact_byte_input.hpp"
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>

class BoundedByteQueue : public ExactByteInput {
public:
  explicit BoundedByteQueue(std::size_t capacity);
  ByteQueueResult append(std::span<const std::uint8_t> bytes);
  ByteQueueResult readExact(std::span<std::uint8_t> destination) override;
  void finish();
  void fail(int errorCode);
  // Stop discards buffered bytes and wakes both blocked readers and producers.
  void stop();
private:
  void end(ByteQueueStatus status, int errorCode);
  std::unique_ptr<std::uint8_t[]> bytes_;
  const std::size_t capacity_;
  std::size_t read_ = 0, write_ = 0, occupied_ = 0;
  ByteQueueStatus status_ = ByteQueueStatus::complete;
  int errorCode_ = 0;
  std::mutex mutex_;
  std::condition_variable readable_, writable_;
};
