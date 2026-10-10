#pragma once

#include <cstddef>
#include <span>
#include <vector>

class PcmOutputQueue {
public:
  PcmOutputQueue() = default;
  PcmOutputQueue(std::size_t capacityFrames, std::size_t bytesPerFrame);
  std::size_t enqueue(std::span<const std::byte> pcm);
  std::size_t copyTo(std::span<std::byte> destination) const;
  void consume(std::size_t bytes);
  void clear();
  std::size_t occupiedBytes() const { return occupied; }
  std::size_t occupiedFrames() const { return frameBytes ? occupied / frameBytes : 0; }

private:
  std::size_t wholeFrames(std::size_t bytes) const;
  std::vector<std::byte> storage;
  std::size_t frameBytes = 0;
  std::size_t readOffset = 0;
  std::size_t occupied = 0;
};
