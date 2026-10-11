#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>

class BufferedFlushPolicy {
  static constexpr std::size_t deferredCapacity = 10;
  static constexpr std::size_t maximumEvents = 2 + 2 * deferredCapacity;
public:
  enum class EventKind {
    immediateStarted, immediateOverrun, immediateCompleted, immediateDiscard,
    deferredCancelled, deferredActivated, deferredCompleted, deferredOverrun, deferredDiscard
  };
  struct Event {
    EventKind kind{};
    std::uint32_t sequence = 0, timestamp = 0;
    std::uint32_t fromSequence = 0, fromTimestamp = 0;
    std::uint32_t untilSequence = 0, untilTimestamp = 0;
    bool immediateWasActive = false;
  };
  class Decision {
  public:
    bool discardCurrent = false;
    std::span<const Event> events() const { return std::span(events_).first(eventCount_); }
  private:
    friend class BufferedFlushPolicy;
    std::array<Event, maximumEvents> events_{};
    std::size_t eventCount_ = 0;
  };
  void requestImmediate(std::uint32_t untilSequence, std::uint32_t untilTimestamp);
  bool requestDeferred(std::uint32_t fromSequence, std::uint32_t fromTimestamp,
                       std::uint32_t untilSequence, std::uint32_t untilTimestamp);
  Decision evaluate(bool everReadBlock, std::uint32_t sequence, std::uint32_t timestamp);
  void resetForBufferedReceiver();
  void clearDeferredForPlayback();
private:
  struct Request {
    bool inUse = false, active = false;
    std::uint32_t fromSequence = 0, fromTimestamp = 0;
    std::uint32_t untilSequence = 0, untilTimestamp = 0;
  };
  void append(Decision &decision, EventKind kind, const Request &request,
              std::uint32_t sequence, std::uint32_t timestamp);
  std::mutex mutex_;
  Request immediate_;
  bool immediateDiagnosticActive_ = false;
  std::array<Request, deferredCapacity> deferred_{};
};
