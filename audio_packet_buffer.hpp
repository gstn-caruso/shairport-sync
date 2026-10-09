#pragma once
#include "queued_audio_packet.hpp"
#include <array>
#include <pthread.h>
#include <variant>

enum class ArrivalKind { first, inOrder, ahead, late, duplicate, tooLate, overflow };
struct MissingAudioPacket { uint16_t sequence; };
using BufferedAudioPacket = std::variant<QueuedAudioPacket, MissingAudioPacket>;

class AudioPacketBuffer {
public:
  struct Admission { ArrivalKind kind; size_t samples; uint64_t revision; };
  struct Front { AudioPacketMetadata packet; uint64_t revision; };
  ~AudioPacketBuffer() {
    reset();
    pthread_cond_destroy(&changed_);
    pthread_mutex_destroy(&mutex_);
  }
  AudioPacketBuffer() = default;
  AudioPacketBuffer(const AudioPacketBuffer &) = delete;
  AudioPacketBuffer &operator=(const AudioPacketBuffer &) = delete;
  template <typename Factory> Admission accept(uint16_t sequence, uint64_t now, Factory factory) {
    Lock lock(mutex_);
    const auto kind = prepareAdmission(sequence, now);
    if (kind == ArrivalKind::tooLate || kind == ArrivalKind::duplicate)
      return {kind, 0, revision_};
    auto packet = factory();
    const auto samples = packet.samplesDecoded();
    entries_[sequence % capacity] = Entry{sequence, now, std::move(packet)};
    if (kind != ArrivalKind::late)
      write_ = static_cast<uint16_t>(sequence + 1);
    advanceRevision();
    return {kind, samples, revision_};
  }
  std::optional<Front> front() const;
  std::optional<BufferedAudioPacket> takeFrontIf(uint64_t revision);
  void reset();
  size_t occupancy() const;
  uint64_t revision() const;
  int waitForChange(uint64_t revision, timespec deadline);

private:
  struct Lock {
    explicit Lock(pthread_mutex_t &mutex) : mutex_(mutex) { pthread_mutex_lock(&mutex_); }
    ~Lock() { pthread_mutex_unlock(&mutex_); }
    pthread_mutex_t &mutex_;
  };
  struct Entry {
    uint16_t sequence;
    uint64_t noticed;
    std::optional<QueuedAudioPacket> packet;
  };
  static constexpr size_t capacity = 1024;
  ArrivalKind prepareAdmission(uint16_t sequence, uint64_t now);
  void resetUnderLock();
  void advanceRevision();
  static void unlockWaitingMutex(void *mutex);
  mutable pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t changed_ = PTHREAD_COND_INITIALIZER;
  std::array<std::optional<Entry>, capacity> entries_;
  uint16_t read_ = 0, write_ = 0;
  bool synced_ = false;
  uint64_t revision_ = 0;
};
