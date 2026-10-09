#include "activity_state.hpp"

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
