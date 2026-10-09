#pragma once

#include "activity_monitor.h"

class ActivityState {
public:
  enum class Effect { none, activate, deactivate };

  am_state status() const noexcept;
  Effect signifyActivity(bool active, double idleTimeout) noexcept;

private:
  am_state state_ = am_inactive;
  bool playerActive_ = false;
};
