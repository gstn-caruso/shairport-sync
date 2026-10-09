#pragma once
#include "volume/volume_policy.hpp"
#include <mutex>

class SharedVolumeLevel {
public:
  explicit SharedVolumeLevel(double initial = -24) : level_(initial) {}
  void remember(double level) { std::lock_guard lock(mutex_); level_ = level; }
  double current() const { std::lock_guard lock(mutex_); return level_; }
private:
  mutable std::mutex mutex_;
  double level_;
};

struct PcmVolumeSnapshot { int gainFixed16 = 65536; bool softwareMuted = false; };
class VolumeControl {
public:
  template <typename Action> void performEffects(Action action) {
    std::lock_guard lock(effectsMutex_);
    action();
  }
  void rememberLevel(double level) { std::lock_guard lock(mutex_); ownLevel_ = level; }
  double suggestedLevel(const SharedVolumeLevel &shared) const {
    std::optional<double> own;
    { std::lock_guard lock(mutex_); own = ownLevel_; }
    return own ? *own : shared.current();
  }
  void apply(const VolumePlan &plan, bool hardwareMuteSucceeded) {
    std::lock_guard lock(mutex_);
    if (plan.gainFixed16) pcm_.gainFixed16 = *plan.gainFixed16;
    if (plan.requestMute && !hardwareMuteSucceeded) pcm_.softwareMuted = true;
    if (plan.unmute) pcm_.softwareMuted = false;
  }
  void resetGainForPlay() { std::lock_guard lock(mutex_); pcm_.gainFixed16 = 65536; }
  PcmVolumeSnapshot pcmSnapshot() const { std::lock_guard lock(mutex_); return pcm_; }
private:
  std::mutex effectsMutex_;
  mutable std::mutex mutex_;
  std::optional<double> ownLevel_;
  PcmVolumeSnapshot pcm_;
};
