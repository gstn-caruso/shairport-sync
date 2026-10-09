#include "monitoring/activity_state.hpp"
#include "monitoring/activity_state.h"

am_state ActivityState::status() const noexcept { return state_; }

ActivityState::Effect ActivityState::signifyActivity(bool active, double idleTimeout) noexcept {
  playerActive_ = active;
  if (state_ == am_inactive && playerActive_) {
    state_ = am_active;
    return Effect::activate;
  }
  if (state_ == am_active && !playerActive_ && idleTimeout == 0.0) {
    state_ = am_inactive;
    return Effect::deactivate;
  }
  return Effect::none;
}

ActivityState::Wait ActivityState::advance() noexcept {
  if (state_ == am_active && !playerActive_) {
    state_ = am_timing_out;
    return Wait::beginTimeout;
  }
  if (state_ == am_timing_out) {
    if (!playerActive_)
      return Wait::deadline;
    state_ = am_active;
  }
  return Wait::signal;
}

ActivityState::Effect ActivityState::timeoutExpired() noexcept {
  if (state_ != am_timing_out)
    return Effect::none;
  if (playerActive_) {
    state_ = am_active;
    return Effect::none;
  }
  state_ = am_inactive;
  return Effect::deactivate;
}

ActivityState::Effect ActivityState::prepareStop() const noexcept {
  return state_ == am_inactive ? Effect::none : Effect::deactivate;
}

ActivityState::Effect ActivityState::stop() noexcept {
  Effect effect = prepareStop();
  state_ = am_inactive;
  return effect;
}

struct activity_state {
  ActivityState value;
};

static activity_effect monitorEffect(ActivityState::Effect effect) noexcept {
  switch (effect) {
  case ActivityState::Effect::none:
    return activity_no_effect;
  case ActivityState::Effect::activate:
    return activity_activate;
  case ActivityState::Effect::deactivate:
    return activity_deactivate;
  }
  return activity_no_effect;
}

activity_state *activity_state_instance(void) {
  static activity_state instance;
  return &instance;
}

void activity_state_reset(activity_state *activity) { activity->value = ActivityState{}; }

am_state activity_state_status(const activity_state *activity) { return activity->value.status(); }

activity_effect activity_state_signify(activity_state *activity, int active, double timeout) {
  return monitorEffect(activity->value.signifyActivity(active != 0, timeout));
}

activity_wait activity_state_advance(activity_state *activity) {
  switch (activity->value.advance()) {
  case ActivityState::Wait::signal:
    return activity_wait_signal;
  case ActivityState::Wait::beginTimeout:
    return activity_begin_timeout;
  case ActivityState::Wait::deadline:
    return activity_wait_deadline;
  }
  return activity_wait_signal;
}

activity_effect activity_state_timeout_expired(activity_state *activity) {
  return monitorEffect(activity->value.timeoutExpired());
}

activity_effect activity_state_stop(activity_state *activity) {
  return monitorEffect(activity->value.stop());
}

activity_effect activity_state_prepare_stop(const activity_state *activity) {
  return monitorEffect(activity->value.prepareStop());
}
