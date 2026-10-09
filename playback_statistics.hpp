#pragma once
#include "audio_arrival.hpp"
#include "playback/timing/playback_sync.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <optional>
#include <cstdint>
#include <mutex>

struct PlaybackStatisticsSnapshot {
  uint64_t packets = 0, late = 0, tooLate = 0;
  double inputFramesPerSecond = 0;
  bool inputRateAvailable = false;
  uint64_t playNumber = 0, missing = 0, resends = 0, frames = 0, measurements = 0;
  int64_t syncErrors = 0, corrections = 0, absoluteCorrections = 0, windowSpreadNs = 0;
  uint64_t minimumDacQueue = UINT64_MAX;
  int32_t minimumBufferOccupancy = INT32_MAX, maximumBufferOccupancy = INT32_MIN;
  int64_t minimumBufferedBytes = -1;
  bool hasObservedFrame = false, outputRateAvailable = false;
  double rawOutputFramesPerSecond = 0, correctedOutputFramesPerSecond = 0;
  double averageSyncErrorMs = 0, averageWindowMs = 0, correctionsPpm = 0, absoluteCorrectionsPpm = 0;
};
struct PlaybackAttempt { uint64_t playNumber; };
struct OutputReading {
  int status;
  uint64_t rawTime, correctedTime, queuedFrames, sentFrames;
};
struct PlaybackSessionSummary {
  bool hasObservedFrame = false, outputRateAvailable = false;
  uint64_t elapsedSeconds = 0;
  double rawOutputFramesPerSecond = 0, correctedOutputFramesPerSecond = 0;
};

class PlaybackStatistics {
public:
  void recordArrival(uint64_t now, uint32_t timestamp, ArrivalKind kind) {
    std::lock_guard lock(mutex_);
    ++totals_.packets;
    ++arrivalsSinceFlush_;
    if (kind == ArrivalKind::late) ++totals_.late;
    if (kind == ArrivalKind::tooLate) ++totals_.tooLate;
    if (kind != ArrivalKind::first && kind != ArrivalKind::inOrder) return;
    if (!inputBaseline_ && arrivalsSinceFlush_ >= 500 && arrivalsSinceFlush_ <= 510) {
      inputBaseline_ = true;
      inputStartTime_ = now;
      inputStartFrame_ = timestamp;
    }
    inputEndTime_ = now;
    inputEndFrame_ = timestamp;
  }
  bool hasArrivals() const {
    std::lock_guard lock(mutex_);
    return totals_.packets != 0;
  }
  void resetInputEpoch() {
    std::lock_guard lock(mutex_);
    arrivalsSinceFlush_ = 0;
    inputBaseline_ = false;
    attemptsSinceFlush_ = 0;
  }
  PlaybackStatisticsSnapshot snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshotUnlocked();
  }
  void recordOutputReading(OutputReading reading) {
    std::lock_guard lock(mutex_);
    const auto played = reading.sentFrames - reading.queuedFrames;
    if (reading.status == 0 && outputBaseline_) {
      const int64_t rawDuration = std::bit_cast<int64_t>(reading.rawTime - outputStart_.rawTime);
      const int64_t correctedDuration = std::bit_cast<int64_t>(reading.correctedTime - outputStart_.correctedTime);
      totals_.outputRateAvailable = rawDuration > 0 && correctedDuration > 0;
      if (totals_.outputRateAvailable) {
        const auto frames = played - (outputStart_.sentFrames - outputStart_.queuedFrames);
        totals_.rawOutputFramesPerSecond = 1e9 * frames / rawDuration;
        totals_.correctedOutputFramesPerSecond = 1e9 * frames / correctedDuration;
        totals_.outputRateAvailable = true;
      }
    }
    if (reading.status != 0 || !outputBaseline_) {
      if (reading.status != 0) totals_.outputRateAvailable = false;
      outputStart_ = reading;
      outputBaseline_ = true;
    }
  }
  void recordMissingPlayback() {
    std::lock_guard lock(mutex_);
    ++totals_.missing;
  }
  void recordResendRequested() {
    std::lock_guard lock(mutex_);
    ++totals_.resends;
  }
  void observeBufferedBytes(uint64_t bytes) {
    std::lock_guard lock(mutex_);
    if (totals_.minimumBufferedBytes < 0 || bytes < uint64_t(totals_.minimumBufferedBytes))
      totals_.minimumBufferedBytes = bytes;
  }
  void recordDacQueue(uint64_t frames) {
    std::lock_guard lock(mutex_);
    totals_.minimumDacQueue = std::min(totals_.minimumDacQueue, frames);
  }
  bool intervalDue(unsigned outputRate) const {
    std::lock_guard lock(mutex_);
    return outputRate != 0 && totals_.frames > uint64_t{8} * outputRate;
  }
  void resetForPlay() {
    std::lock_guard lock(mutex_);
    totals_ = {};
    arrivalsSinceFlush_ = attemptsSinceFlush_ = playStart_ = 0;
    inputBaseline_ = outputBaseline_ = hasObservedFrame_ = false;
  }
  PlaybackAttempt recordPlaybackAttempt(uint64_t now) {
    std::lock_guard lock(mutex_);
    if (totals_.playNumber == 0) playStart_ = now;
    ++attemptsSinceFlush_;
    return {++totals_.playNumber};
  }
  bool hasPlaybackSinceFlush() const {
    std::lock_guard lock(mutex_);
    return attemptsSinceFlush_ != 0;
  }
  bool observeFrame(int32_t occupancy) {
    std::lock_guard lock(mutex_);
    const bool first = !hasObservedFrame_;
    hasObservedFrame_ = totals_.hasObservedFrame = true;
    totals_.minimumBufferOccupancy = std::min(totals_.minimumBufferOccupancy, occupancy);
    totals_.maximumBufferOccupancy = std::max(totals_.maximumBufferOccupancy, occupancy);
    return first;
  }
  void recordSync(const SyncDecision &decision) {
    std::lock_guard lock(mutex_);
    totals_.windowSpreadNs += decision.windowSpreadNs;
  }
  void recordSubmitted(uint64_t frames, int64_t error, int correction) {
    std::lock_guard lock(mutex_);
    totals_.frames += frames;
    if (frames == 0) return;
    ++totals_.measurements;
    totals_.syncErrors += error;
    totals_.corrections += correction;
    totals_.absoluteCorrections += std::abs(correction);
  }
  std::optional<PlaybackStatisticsSnapshot> takeIntervalIfDue(unsigned outputRate) {
    std::lock_guard lock(mutex_);
    if (outputRate == 0 || totals_.frames <= uint64_t{8} * outputRate) return std::nullopt;
    auto result = snapshotUnlocked();
    if (result.measurements != 0) {
      result.averageSyncErrorMs = 1000.0 * result.syncErrors / (result.measurements * double(outputRate));
      result.averageWindowMs = 1e-6 * result.windowSpreadNs / result.measurements;
      result.correctionsPpm = 1e6 * result.corrections / result.frames;
      result.absoluteCorrectionsPpm = 1e6 * result.absoluteCorrections / result.frames;
    }
    totals_.frames = totals_.measurements = 0;
    totals_.syncErrors = totals_.corrections = totals_.absoluteCorrections = totals_.windowSpreadNs = 0;
    totals_.minimumDacQueue = UINT64_MAX;
    totals_.minimumBufferOccupancy = INT32_MAX;
    totals_.maximumBufferOccupancy = INT32_MIN;
    totals_.minimumBufferedBytes = -1;
    totals_.hasObservedFrame = false;
    return result;
  }
  PlaybackSessionSummary sessionSummary(uint64_t now) const {
    std::lock_guard lock(mutex_);
    return {hasObservedFrame_, totals_.outputRateAvailable,
      totals_.playNumber == 0 ? 0 : (now - playStart_) / 1000000000,
      totals_.rawOutputFramesPerSecond, totals_.correctedOutputFramesPerSecond};
  }
private:
  PlaybackStatisticsSnapshot snapshotUnlocked() const {
    auto result = totals_;
    const auto duration = inputEndTime_ - inputStartTime_;
    result.inputRateAvailable = inputBaseline_ && duration != 0;
    result.inputFramesPerSecond = result.inputRateAvailable ?
      1e9 * uint32_t(inputEndFrame_ - inputStartFrame_) / duration : 0;
    return result;
  }
  mutable std::mutex mutex_;
  PlaybackStatisticsSnapshot totals_;
  uint64_t arrivalsSinceFlush_ = 0, inputStartTime_ = 0, inputEndTime_ = 0;
  uint32_t inputStartFrame_ = 0, inputEndFrame_ = 0;
  bool inputBaseline_ = false;
  uint64_t attemptsSinceFlush_ = 0, playStart_ = 0;
  bool hasObservedFrame_ = false;
  OutputReading outputStart_{};
  bool outputBaseline_ = false;
};
