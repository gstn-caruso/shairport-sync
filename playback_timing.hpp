#pragma once
#include "audio_arrival.hpp"
#include <cstdint>
#include <mutex>
#include <optional>

struct PrerollObservation {
  uint64_t now;
  std::optional<uint64_t> firstFrameTime;
  bool hasDelay;
  bool delayMeasured = false;
  int delayStatus = 0;
  int64_t delayFrames = 0;
};
struct PrerollPolicy { unsigned outputRate; bool automaticLeadIn; int64_t leadInNs; };
struct PrerollAction {
  uint64_t revision = 0, silenceFrames = 0, maximumChunkFrames = 0;
  bool queryDelay = false;
};
struct ReleaseObservation {
  uint64_t now;
  std::optional<uint64_t> targetTime;
  int delayStatus = -1;
  int64_t delayFrames = 0;
};
class PlaybackTiming {
public:
  struct Start { uint32_t timestamp; uint64_t revision; bool configureOutput; };
  struct ReleaseTarget { uint32_t timestamp, frame, desiredFrames; uint64_t revision; };
  void resetForPlay();
  void beginPacketWait();
  void onBufferReset();
  void onFlush();
  void onResync();
  void onArrival(ArrivalKind);
  std::optional<Start> startWithReadyPacket(uint32_t timestamp);
  PrerollAction planPreroll(Start, PrerollObservation, PrerollPolicy);
  bool mayApply(PrerollAction) const;
  void markSilenceSubmitted(PrerollAction);
  ReleaseTarget releaseTargetFrame(uint32_t timestamp, uint32_t desiredFrames) const;
  bool shouldRelease(ReleaseTarget, ReleaseObservation) const;
  bool isFirstFrame(uint32_t timestamp) const;
private:
  void restart(bool primed);
  mutable std::mutex mutex_;
  bool buffering_ = true, primed_ = false;
  uint32_t firstTimestamp_ = 0;
  uint64_t firstTime_ = 0, revision_ = 0;
};
