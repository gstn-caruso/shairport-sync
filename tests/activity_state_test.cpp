#include "activity_state.hpp"
#include <cassert>

int main() {
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
