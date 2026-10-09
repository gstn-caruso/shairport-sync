#include "activity_state.hpp"
#include <cassert>

static void checkImmediateActivity() {
  ActivityState activity;
  assert(activity.status() == am_inactive);
  assert(activity.signifyActivity(false, 0.0) == ActivityState::Effect::none);
  assert(activity.signifyActivity(true, 0.0) == ActivityState::Effect::activate);
  assert(activity.status() == am_active);
  assert(activity.signifyActivity(true, 0.0) == ActivityState::Effect::none);
  assert(activity.signifyActivity(false, 0.0) == ActivityState::Effect::deactivate);
  assert(activity.status() == am_inactive);
  assert(activity.signifyActivity(false, 0.0) == ActivityState::Effect::none);
}

static void checkDelayedActivityAndReactivation() {
  ActivityState delayed;
  assert(delayed.advance() == ActivityState::Wait::signal);
  delayed.signifyActivity(true, 2.0);
  assert(delayed.advance() == ActivityState::Wait::signal);
  assert(delayed.signifyActivity(false, 2.0) == ActivityState::Effect::none);
  assert(delayed.status() == am_active);
  assert(delayed.advance() == ActivityState::Wait::beginTimeout);
  assert(delayed.status() == am_timing_out);
  assert(delayed.advance() == ActivityState::Wait::deadline);
  assert(delayed.signifyActivity(true, 2.0) == ActivityState::Effect::none);
  assert(delayed.advance() == ActivityState::Wait::signal);
  assert(delayed.status() == am_active);
}

static void checkExpiration() {
  ActivityState expired;
  expired.signifyActivity(true, 2.0);
  assert(expired.timeoutExpired() == ActivityState::Effect::none);
  expired.signifyActivity(false, 2.0);
  assert(expired.advance() == ActivityState::Wait::beginTimeout);
  assert(expired.timeoutExpired() == ActivityState::Effect::deactivate);
  assert(expired.status() == am_inactive);
  assert(expired.timeoutExpired() == ActivityState::Effect::none);
  expired.signifyActivity(true, 2.0);
  expired.signifyActivity(false, 2.0);
  expired.advance();
  expired.signifyActivity(true, 2.0);
  assert(expired.timeoutExpired() == ActivityState::Effect::none);
  assert(expired.status() == am_active);
}

static void checkStop() {
  ActivityState stopped;
  assert(stopped.stop() == ActivityState::Effect::none);
  stopped.signifyActivity(true, 2.0);
  assert(stopped.stop() == ActivityState::Effect::deactivate);
  assert(stopped.status() == am_inactive);
  assert(stopped.advance() == ActivityState::Wait::signal);
  assert(stopped.stop() == ActivityState::Effect::none);
  stopped.signifyActivity(true, 2.0);
  stopped.signifyActivity(false, 2.0);
  stopped.advance();
  assert(stopped.stop() == ActivityState::Effect::deactivate);
  assert(stopped.status() == am_inactive);
  assert(stopped.advance() == ActivityState::Wait::signal);
}

int main() {
  assert(activity_status() == am_inactive);
  checkImmediateActivity();
  checkDelayedActivityAndReactivation();
  checkExpiration();
  checkStop();
}
