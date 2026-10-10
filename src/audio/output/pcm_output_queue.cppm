module;
#include <cstddef>
#include <memory>
#include <span>

export module receiver.audio.output.queue;

export class PcmOutputQueue {
public:
  PcmOutputQueue();
  PcmOutputQueue(std::size_t capacityFrames, std::size_t bytesPerFrame);
  ~PcmOutputQueue();
  PcmOutputQueue(PcmOutputQueue &&) noexcept;
  PcmOutputQueue &operator=(PcmOutputQueue &&) noexcept;
  std::size_t enqueue(std::span<const std::byte> pcm);
  std::size_t copyTo(std::span<std::byte> destination) const;
  void consume(std::size_t bytes);
  void clear();
  std::size_t occupiedBytes() const;
  std::size_t occupiedFrames() const;

private:
  struct Storage;
  std::unique_ptr<Storage> storage;
};
