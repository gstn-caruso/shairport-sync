#include "playback/timing/playback_timing.hpp"
#include <algorithm>
#include <bit>

static int64_t framesFor(int64_t nanoseconds, unsigned rate) {
  return nanoseconds / 1000000000 * rate + nanoseconds % 1000000000 * rate / 1000000000;
}
void PlaybackTiming::restart(bool primed) {
  buffering_ = true;
  firstTimestamp_ = 0;
  firstTime_ = 0;
  primed_ = primed;
  ++revision_;
}
void PlaybackTiming::resetForPlay() { std::lock_guard lock(mutex_); restart(false); }
void PlaybackTiming::onFlush() { std::lock_guard lock(mutex_); restart(false); }
void PlaybackTiming::onResync() { std::lock_guard lock(mutex_); restart(true); }
void PlaybackTiming::beginPacketWait() {
  std::lock_guard lock(mutex_);
  primed_ = false;
  ++revision_;
}
void PlaybackTiming::onBufferReset() {
  std::lock_guard lock(mutex_);
  buffering_ = true;
  ++revision_;
}
void PlaybackTiming::onArrival(ArrivalKind kind) {
  std::lock_guard lock(mutex_);
  if (kind != ArrivalKind::first && kind != ArrivalKind::overflow) return;
  firstTimestamp_ = 0;
  if (kind == ArrivalKind::overflow) buffering_ = true;
  ++revision_;
}
std::optional<PlaybackTiming::Start> PlaybackTiming::startWithReadyPacket(uint32_t timestamp) {
  std::lock_guard lock(mutex_);
  if (!buffering_) return std::nullopt;
  const bool configure = firstTimestamp_ == 0;
  if (configure) firstTimestamp_ = timestamp;
  return Start{firstTimestamp_, revision_, configure};
}
PrerollAction PlaybackTiming::planPreroll(Start start, PrerollObservation observation, PrerollPolicy policy) {
  std::lock_guard lock(mutex_);
  PrerollAction action{revision_};
  if (start.revision != revision_ || !buffering_ || policy.outputRate == 0) return action;
  if (start.configureOutput || observation.hasDelay) {
    if (!observation.firstFrameTime) {
      firstTimestamp_ = 0;
      firstTime_ = 0;
      return action;
    }
    if (start.configureOutput) firstTimestamp_ = start.timestamp;
    firstTime_ = *observation.firstFrameTime;
  }
  if (firstTime_ == 0) return action;
  const int64_t lead = std::bit_cast<int64_t>(firstTime_ - observation.now);
  action.maximumChunkFrames = policy.outputRate / 10;
  if (!observation.hasDelay) {
    const auto gap = framesFor(lead, policy.outputRate);
    if (gap > 0) action.silenceFrames = gap;
    buffering_ = false;
    return action;
  }
  if (lead < 0) { buffering_ = false; return action; }
  if (!policy.automaticLeadIn && lead > policy.leadInNs) return action;
  if (primed_ && !observation.delayMeasured) { action.queryDelay = true; return action; }
  if (primed_ && observation.delayStatus != 0) return action;
  const auto gap = framesFor(lead, policy.outputRate) - (primed_ ? observation.delayFrames : 0);
  if (gap < int64_t(action.maximumChunkFrames)) buffering_ = false;
  if (gap > 0) action.silenceFrames = std::min(uint64_t(gap), action.maximumChunkFrames);
  return action;
}
bool PlaybackTiming::mayApply(PrerollAction action) const {
  std::lock_guard lock(mutex_);
  return action.revision == revision_;
}
void PlaybackTiming::markSilenceSubmitted(PrerollAction action) {
  std::lock_guard lock(mutex_);
  if (action.revision == revision_) primed_ = true;
}
PlaybackTiming::ReleaseTarget PlaybackTiming::releaseTargetFrame(uint32_t timestamp, uint32_t desiredFrames) const {
  std::lock_guard lock(mutex_);
  return {timestamp, timestamp - desiredFrames, desiredFrames, revision_};
}
bool PlaybackTiming::shouldRelease(ReleaseTarget target, ReleaseObservation observation) const {
  std::lock_guard lock(mutex_);
  if (target.revision != revision_ || buffering_) return false;
  if (target.timestamp == 0) return true;
  if (!observation.targetTime) return false;
  if ((firstTimestamp_ == target.timestamp || observation.delayStatus != 0) && observation.now >= *observation.targetTime)
    return true;
  if (firstTimestamp_ != target.timestamp && observation.delayStatus == 0 &&
      uint64_t(std::max(int64_t{0}, observation.delayFrames)) < target.desiredFrames) return true;
  const auto difference = std::bit_cast<int64_t>(observation.now - *observation.targetTime);
  return difference > 10000000000 || difference < -10000000000;
}
bool PlaybackTiming::isFirstFrame(uint32_t timestamp) const {
  std::lock_guard lock(mutex_);
  return firstTimestamp_ == timestamp;
}
