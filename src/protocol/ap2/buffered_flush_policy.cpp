#include "protocol/ap2/buffered_flush_policy.hpp"
#include "platform/utilities/mod23.h"

void BufferedFlushPolicy::requestImmediate(std::uint32_t sequence, std::uint32_t timestamp) {
  std::lock_guard lock(mutex_);
  immediate_.inUse = true;
  immediate_.untilSequence = sequence & 0x7fffff;
  immediate_.untilTimestamp = timestamp;
}
bool BufferedFlushPolicy::requestDeferred(std::uint32_t fromSequence, std::uint32_t fromTimestamp,
                                         std::uint32_t untilSequence, std::uint32_t untilTimestamp) {
  std::lock_guard lock(mutex_);
  for (auto &request : deferred_) {
    if (!request.inUse) {
      request = {true, false, fromSequence & 0x7fffff, fromTimestamp,
                 untilSequence & 0x7fffff, untilTimestamp};
      return true;
    }
  }
  return false;
}
void BufferedFlushPolicy::append(Decision &decision, EventKind kind, const Request &request,
                                 std::uint32_t sequence, std::uint32_t timestamp) {
  decision.events_[decision.eventCount_++] = {kind, sequence, timestamp,
      request.fromSequence, request.fromTimestamp, request.untilSequence, request.untilTimestamp,
      immediateDiagnosticActive_};
}
BufferedFlushPolicy::Decision BufferedFlushPolicy::evaluate(bool everReadBlock,
                                                           std::uint32_t sequence,
                                                           std::uint32_t timestamp) {
  std::lock_guard lock(mutex_);
  Decision decision;
  sequence &= 0x7fffff;
  if (everReadBlock && immediate_.inUse) {
    if (!immediateDiagnosticActive_)
      append(decision, EventKind::immediateStarted, immediate_, sequence, timestamp);
    const auto difference = a_minus_b_mod23(sequence, immediate_.untilSequence);
    if (difference > 0)
      append(decision, EventKind::immediateOverrun, immediate_, sequence, timestamp);
    if (difference >= 0) {
      append(decision, EventKind::immediateCompleted, immediate_, sequence, timestamp);
      immediate_.inUse = false;
      immediateDiagnosticActive_ = false;
      for (auto &request : deferred_) {
        if (request.inUse && !request.active)
          append(decision, EventKind::deferredCancelled, request, sequence, timestamp);
        request.inUse = request.active = false;
      }
    } else {
      append(decision, EventKind::immediateDiscard, immediate_, sequence, timestamp);
      immediateDiagnosticActive_ = true;
      decision.discardCurrent = true;
    }
  }
  for (auto &request : deferred_) {
    if (!request.inUse)
      continue;
    if (request.fromSequence == sequence && request.untilSequence != sequence) {
      append(decision, EventKind::deferredActivated, request, sequence, timestamp);
      request.active = true;
      decision.discardCurrent = true;
    }
    if (request.untilSequence == sequence) {
      append(decision, EventKind::deferredCompleted, request, sequence, timestamp);
      request.active = request.inUse = false;
    } else if (a_minus_b_mod23(sequence, request.untilSequence) > 0) {
      append(decision, EventKind::deferredOverrun, request, sequence, timestamp);
      request.active = request.inUse = false;
    } else if (request.active) {
      append(decision, EventKind::deferredDiscard, request, sequence, timestamp);
      decision.discardCurrent = true;
    }
  }
  return decision;
}
void BufferedFlushPolicy::resetForBufferedReceiver() {}
void BufferedFlushPolicy::clearDeferredForPlayback() {}
