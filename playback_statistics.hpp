#pragma once
#include "audio_arrival.hpp"
#include <cstdint>
#include <mutex>

struct PlaybackStatisticsSnapshot {
  uint64_t packets = 0, late = 0, tooLate = 0;
  double inputFramesPerSecond = 0;
  bool inputRateAvailable = false;
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
  }
  PlaybackStatisticsSnapshot snapshot() const {
    std::lock_guard lock(mutex_);
    auto result = totals_;
    const auto duration = inputEndTime_ - inputStartTime_;
    result.inputRateAvailable = inputBaseline_ && duration != 0;
    if (result.inputRateAvailable)
      result.inputFramesPerSecond = 1e9 * uint32_t(inputEndFrame_ - inputStartFrame_) / duration;
    return result;
  }
private:
  mutable std::mutex mutex_;
  PlaybackStatisticsSnapshot totals_;
  uint64_t arrivalsSinceFlush_ = 0, inputStartTime_ = 0, inputEndTime_ = 0;
  uint32_t inputStartFrame_ = 0, inputEndFrame_ = 0;
  bool inputBaseline_ = false;
};
