#pragma once
#include "volume/volume_policy.hpp"
#include <mutex>

class SharedVolumeLevel {
public:
  explicit SharedVolumeLevel(AirPlayVolume initial = AirPlayVolume{-24}) : level_(initial) {}
  void remember(AirPlayVolume level) { std::lock_guard lock(mutex_); level_ = level; }
  AirPlayVolume current() const { std::lock_guard lock(mutex_); return level_; }
private:
  mutable std::mutex mutex_;
  AirPlayVolume level_;
};

struct PcmVolumeSnapshot { FixedGain16 gainFixed16 = FixedGain16::unity(); bool softwareMuted = false; };
class VolumeControl {
public:
  template <typename Action> void performEffects(Action action) {
    std::lock_guard lock(effectsMutex_);
    action();
  }
  void rememberLevel(AirPlayVolume level) { std::lock_guard lock(mutex_); ownLevel_ = level; }
  AirPlayVolume suggestedLevel(const SharedVolumeLevel &shared) const {
    std::optional<AirPlayVolume> own;
    { std::lock_guard lock(mutex_); own = ownLevel_; }
    return own ? *own : shared.current();
  }
  void apply(const VolumePlan &plan, bool hardwareMuteSucceeded) {
    std::lock_guard lock(mutex_);
    if (plan.gainFixed16) pcm_.gainFixed16 = *plan.gainFixed16;
    if (plan.requestMute && !hardwareMuteSucceeded) pcm_.softwareMuted = true;
    if (plan.unmute) pcm_.softwareMuted = false;
  }
  void resetGainForPlay() { std::lock_guard lock(mutex_); pcm_.gainFixed16 = FixedGain16::unity(); }
  PcmVolumeSnapshot pcmSnapshot() const { std::lock_guard lock(mutex_); return pcm_; }
private:
  std::mutex effectsMutex_;
  mutable std::mutex mutex_;
  std::optional<AirPlayVolume> ownLevel_;
  PcmVolumeSnapshot pcm_;
};
