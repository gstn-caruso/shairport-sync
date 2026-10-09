#pragma once

#include "activity_monitor.h"

class ActivityState {
public:
  enum class Effect { none, activate, deactivate };
  enum class Wait { signal, beginTimeout, deadline };

  am_state status() const noexcept;
  Effect signifyActivity(bool active, double idleTimeout) noexcept;
  Wait advance() noexcept;
  Effect timeoutExpired() noexcept;
  Effect stop() noexcept;

private:
  am_state state_ = am_inactive;
  bool playerActive_ = false;
};
