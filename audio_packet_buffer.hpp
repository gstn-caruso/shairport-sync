#pragma once
#include "queued_audio_packet.hpp"
#include "retransmission_planner.hpp"
#include <array>
#include <pthread.h>
#include <variant>

enum class ArrivalKind { first, inOrder, ahead, late, duplicate, tooLate, overflow };
struct MissingAudioPacket { uint16_t sequence; };
using BufferedAudioPacket = std::variant<QueuedAudioPacket, MissingAudioPacket>;

class AudioPacketBuffer {
public:
  struct Admission {
    ArrivalKind kind;
    size_t samples;
    uint64_t revision;
    std::vector<ResendRange> resendRanges{};
  };
  struct Front { AudioPacketMetadata packet; uint64_t revision; AVSampleFormat sampleFormat; };
  struct FlushEffect {
    uint64_t id = 0;
    bool flushOutput = false, resetTiming = false, complete = false;
    size_t discarded = 0;
  };
  ~AudioPacketBuffer() {
    reset();
    pthread_cond_destroy(&changed_);
    pthread_mutex_destroy(&mutex_);
  }
  AudioPacketBuffer() = default;
  AudioPacketBuffer(const AudioPacketBuffer &) = delete;
  AudioPacketBuffer &operator=(const AudioPacketBuffer &) = delete;
  template <typename Factory> Admission accept(uint16_t sequence, uint64_t now, Factory factory,
                                               RetryPolicy policy = {}) {
    Lock lock(mutex_);
    const auto kind = classifyArrival(sequence);
    if (kind == ArrivalKind::tooLate || kind == ArrivalKind::duplicate)
      return {kind, 0, revision_, planner_.due(now, policy, {read_, write_})};
    auto packet = factory();
    const auto samples = packet.samplesDecoded();
    makeRoomFor(sequence, now, kind);
    entries_[sequence % capacity] = Entry{sequence, std::move(packet)};
    planner_.resolve(sequence);
    if (kind != ArrivalKind::late)
      write_ = static_cast<uint16_t>(sequence + 1);
    advanceRevision();
    return {kind, samples, revision_, planner_.due(now, policy, {read_, write_})};
  }
  std::optional<Front> front() const;
  std::optional<BufferedAudioPacket> takeFrontIf(uint64_t revision);
  void reset();
  size_t occupancy() const;
  uint64_t revision() const;
  int waitForChange(uint64_t revision, timespec deadline);
  uint64_t requestFlush(uint32_t timestamp);
  FlushEffect applyFlush();
  size_t discardPacketsStartingBefore(uint32_t timestamp);
  std::vector<ResendRange> due(uint64_t now, RetryPolicy policy);

private:
  struct Lock {
    explicit Lock(pthread_mutex_t &mutex) : mutex_(mutex) { pthread_mutex_lock(&mutex_); }
    ~Lock() { pthread_mutex_unlock(&mutex_); }
    pthread_mutex_t &mutex_;
  };
  struct Entry {
    uint16_t sequence;
    std::optional<QueuedAudioPacket> packet;
  };
  struct FlushRequest { uint64_t id; uint32_t timestamp; bool delivered = false; };
  static constexpr size_t capacity = 1024;
  ArrivalKind classifyArrival(uint16_t sequence) const;
  void makeRoomFor(uint16_t sequence, uint64_t now, ArrivalKind kind);
  void resetUnderLock();
  void advanceRevision();
  static void unlockWaitingMutex(void *mutex);
  void discardFrontUnderLock();
  mutable pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t changed_ = PTHREAD_COND_INITIALIZER;
  std::array<std::optional<Entry>, capacity> entries_;
  uint16_t read_ = 0, write_ = 0;
  bool synced_ = false;
  uint64_t revision_ = 0;
  RetransmissionPlanner planner_;
  std::optional<FlushRequest> flush_;
  uint64_t nextFlushId_ = 0;
};
