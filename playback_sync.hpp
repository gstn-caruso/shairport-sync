#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

struct SyncObservation {
  uint64_t expectedFrameTime = 0, dacMeasurementTime = 0;
  int64_t measurementDuration = 0;
  uint64_t framesInDac = 0, retainedFrames = 0;
  uint32_t timestamp = 0;
  int32_t timestampGap = 0;
  bool measured = false, firstFrame = false;
  unsigned inputRate = 0, outputRate = 0;
  size_t blockFrames = 0, playNumber = 0;
};
struct SyncPolicy { bool enabled; int64_t toleranceNs; double resyncThresholdSeconds; };
struct SyncDecision {
  int64_t errorNs = 0, errorFrames = 0, windowSpreadNs = 0;
  int correctionFrames = 0;
  uint64_t silenceFrames = 0;
  bool dropPacket = false, resyncNext = false;
};

class PlaybackSync {
public:
  SyncDecision observe(const SyncObservation &, SyncPolicy);
  size_t skipFrom(size_t encodedFrames);
  void resetForPlay();
private:
  struct Window {
    void add(int64_t error);
    int64_t center() const;
    int64_t spread() const;
    bool full() const { return count == values.size(); }
    bool crossesZero() const { return highest >= 0 && lowest <= 0; }
    std::array<int64_t, 40> values{};
    size_t next = 0, count = 0;
    int64_t highest = 0, lowest = 0, secondHighest = 0, secondLowest = 0;
  } window_;
  uint64_t previousRetained_ = 0, pendingSkip_ = 0;
  bool skippingInitial_ = false;
};
