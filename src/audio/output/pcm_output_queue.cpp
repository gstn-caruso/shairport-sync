module;
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

module receiver.audio.output.queue;

struct PcmOutputQueue::Storage {
  Storage(std::size_t capacityFrames, std::size_t bytesPerFrame)
      : bytes(capacityFrames * bytesPerFrame), frameBytes(bytesPerFrame) {}
  std::size_t wholeFrames(std::size_t count) const {
    return frameBytes ? count - count % frameBytes : 0;
  }
  std::vector<std::byte> bytes;
  std::size_t frameBytes;
  std::size_t readOffset = 0;
  std::size_t occupied = 0;
};

PcmOutputQueue::PcmOutputQueue() = default;
PcmOutputQueue::~PcmOutputQueue() = default;
PcmOutputQueue::PcmOutputQueue(PcmOutputQueue &&) noexcept = default;
PcmOutputQueue &PcmOutputQueue::operator=(PcmOutputQueue &&) noexcept = default;

PcmOutputQueue::PcmOutputQueue(std::size_t capacityFrames, std::size_t bytesPerFrame)
    : storage(std::make_unique<Storage>(capacityFrames, bytesPerFrame)) {}

std::size_t PcmOutputQueue::occupiedBytes() const {
  return storage ? storage->occupied : 0;
}

std::size_t PcmOutputQueue::occupiedFrames() const {
  return storage && storage->frameBytes ? storage->occupied / storage->frameBytes : 0;
}

std::size_t PcmOutputQueue::enqueue(std::span<const std::byte> pcm) {
  if (!storage) return 0;
  auto &[buffer, frameBytes, readOffset, occupied] = *storage;
  const auto bytes = storage->wholeFrames(std::min(pcm.size(), buffer.size() - occupied));
  if (bytes == 0) return 0;
  const auto writeOffset = (readOffset + occupied) % buffer.size();
  const auto first = std::min(bytes, buffer.size() - writeOffset);
  std::memcpy(buffer.data() + writeOffset, pcm.data(), first);
  std::memcpy(buffer.data(), pcm.data() + first, bytes - first);
  occupied += bytes;
  return bytes;
}

std::size_t PcmOutputQueue::copyTo(std::span<std::byte> destination) const {
  if (!storage) return 0;
  const auto &[buffer, frameBytes, readOffset, occupied] = *storage;
  const auto bytes = storage->wholeFrames(std::min(destination.size(), occupied));
  if (bytes == 0) return 0;
  const auto first = std::min(bytes, buffer.size() - readOffset);
  std::memcpy(destination.data(), buffer.data() + readOffset, first);
  std::memcpy(destination.data() + first, buffer.data(), bytes - first);
  return bytes;
}

void PcmOutputQueue::consume(std::size_t bytes) {
  if (!storage) return;
  auto &[buffer, frameBytes, readOffset, occupied] = *storage;
  bytes = storage->wholeFrames(std::min(bytes, occupied));
  if (bytes == 0) return;
  readOffset = (readOffset + bytes) % buffer.size();
  occupied -= bytes;
}

void PcmOutputQueue::clear() {
  if (!storage) return;
  storage->readOffset = storage->occupied = 0;
}
