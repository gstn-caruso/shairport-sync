#include "pcm_output_queue.hpp"
#include <algorithm>
#include <cstring>

PcmOutputQueue::PcmOutputQueue(std::size_t capacityFrames, std::size_t bytesPerFrame)
    : storage(capacityFrames * bytesPerFrame), frameBytes(bytesPerFrame) {}

std::size_t PcmOutputQueue::wholeFrames(std::size_t bytes) const {
  return frameBytes ? bytes - bytes % frameBytes : 0;
}

std::size_t PcmOutputQueue::enqueue(std::span<const std::byte> pcm) {
  const auto bytes = wholeFrames(std::min(pcm.size(), storage.size() - occupied));
  if (bytes == 0) return 0;
  const auto writeOffset = (readOffset + occupied) % storage.size();
  const auto first = std::min(bytes, storage.size() - writeOffset);
  std::memcpy(storage.data() + writeOffset, pcm.data(), first);
  std::memcpy(storage.data(), pcm.data() + first, bytes - first);
  occupied += bytes;
  return bytes;
}

std::size_t PcmOutputQueue::copyTo(std::span<std::byte> destination) const {
  const auto bytes = wholeFrames(std::min(destination.size(), occupied));
  if (bytes == 0) return 0;
  const auto first = std::min(bytes, storage.size() - readOffset);
  std::memcpy(destination.data(), storage.data() + readOffset, first);
  std::memcpy(destination.data() + first, storage.data(), bytes - first);
  return bytes;
}

void PcmOutputQueue::consume(std::size_t bytes) {
  bytes = wholeFrames(std::min(bytes, occupied));
  if (bytes == 0) return;
  readOffset = (readOffset + bytes) % storage.size();
  occupied -= bytes;
}

void PcmOutputQueue::clear() {
  readOffset = occupied = 0;
}
