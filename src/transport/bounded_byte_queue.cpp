#include "transport/bounded_byte_queue.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

BoundedByteQueue::BoundedByteQueue(std::size_t capacity)
    : bytes_(capacity ? std::make_unique_for_overwrite<std::uint8_t[]>(capacity) : nullptr), capacity_(capacity) {
  if (capacity == 0)
    throw std::invalid_argument("Byte queue capacity must be positive");
}
ByteQueueResult BoundedByteQueue::append(std::span<const std::uint8_t> bytes) {
  std::unique_lock lock(mutex_);
  std::size_t accepted = 0;
  while (accepted < bytes.size()) {
    writable_.wait(lock, [&] { return occupied_ < capacity_ || status_ != ByteQueueStatus::complete; });
    if (status_ != ByteQueueStatus::complete)
      return {status_, accepted, occupied_, errorCode_};
    const auto count = std::min({bytes.size() - accepted, capacity_ - occupied_, capacity_ - write_});
    std::memcpy(bytes_.get() + write_, bytes.data() + accepted, count);
    write_ = (write_ + count) % capacity_;
    occupied_ += count;
    accepted += count;
    readable_.notify_all();
  }
  return {ByteQueueStatus::complete, accepted, occupied_};
}
ByteQueueResult BoundedByteQueue::readExact(std::span<std::uint8_t> destination) {
  std::unique_lock lock(mutex_);
  std::size_t received = 0;
  while (received < destination.size()) {
    readable_.wait(lock, [&] { return occupied_ != 0 || status_ != ByteQueueStatus::complete; });
    if (occupied_ == 0)
      return {status_, received, 0, errorCode_};
    const auto count = std::min({destination.size() - received, occupied_, capacity_ - read_});
    std::memcpy(destination.data() + received, bytes_.get() + read_, count);
    read_ = (read_ + count) % capacity_;
    occupied_ -= count;
    received += count;
    writable_.notify_all();
  }
  return {ByteQueueStatus::complete, received, occupied_};
}
void BoundedByteQueue::end(ByteQueueStatus status, int errorCode) {
  std::lock_guard lock(mutex_);
  if (status_ == ByteQueueStatus::complete) {
    status_ = status;
    errorCode_ = errorCode;
  }
  readable_.notify_all();
  writable_.notify_all();
}
void BoundedByteQueue::finish() { end(ByteQueueStatus::endOfStream, 0); }
void BoundedByteQueue::fail(int errorCode) { end(ByteQueueStatus::error, errorCode); }
void BoundedByteQueue::stop() {
  std::lock_guard lock(mutex_);
  status_ = ByteQueueStatus::stopped;
  errorCode_ = 0;
  occupied_ = 0;
  readable_.notify_all();
  writable_.notify_all();
}
