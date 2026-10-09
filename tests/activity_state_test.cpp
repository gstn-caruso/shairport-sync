#include "activity_state.hpp"
#include <gtest/gtest.h>

TEST(ActivityState, ImmediateActivationAndDeactivationAreIdempotent) {
  ActivityState activity;
  EXPECT_EQ(activity.status(), am_inactive);
  EXPECT_EQ(activity.signifyActivity(false, 0.0), ActivityState::Effect::none);
  EXPECT_EQ(activity.signifyActivity(true, 0.0), ActivityState::Effect::activate);
  EXPECT_EQ(activity.status(), am_active);
  EXPECT_EQ(activity.signifyActivity(true, 0.0), ActivityState::Effect::none);
  EXPECT_EQ(activity.signifyActivity(false, 0.0), ActivityState::Effect::deactivate);
  EXPECT_EQ(activity.status(), am_inactive);
  EXPECT_EQ(activity.signifyActivity(false, 0.0), ActivityState::Effect::none);
}

TEST(ActivityState, DelayedInactivityStartsTimeoutAndReactivationCancelsIt) {
  ActivityState delayed;
  EXPECT_EQ(delayed.advance(), ActivityState::Wait::signal);
  delayed.signifyActivity(true, 2.0);
  EXPECT_EQ(delayed.advance(), ActivityState::Wait::signal);
  EXPECT_EQ(delayed.signifyActivity(false, 2.0), ActivityState::Effect::none);
  EXPECT_EQ(delayed.status(), am_active);
  EXPECT_EQ(delayed.advance(), ActivityState::Wait::beginTimeout);
  EXPECT_EQ(delayed.status(), am_timing_out);
  EXPECT_EQ(delayed.advance(), ActivityState::Wait::deadline);
  EXPECT_EQ(delayed.signifyActivity(true, 2.0), ActivityState::Effect::none);
  EXPECT_EQ(delayed.advance(), ActivityState::Wait::signal);
  EXPECT_EQ(delayed.status(), am_active);
}

TEST(ActivityState, ExpirationDeactivatesOnlyWhileTimingOut) {
  ActivityState expired;
  expired.signifyActivity(true, 2.0);
  EXPECT_EQ(expired.timeoutExpired(), ActivityState::Effect::none);
  expired.signifyActivity(false, 2.0);
  EXPECT_EQ(expired.advance(), ActivityState::Wait::beginTimeout);
  EXPECT_EQ(expired.timeoutExpired(), ActivityState::Effect::deactivate);
  EXPECT_EQ(expired.status(), am_inactive);
  EXPECT_EQ(expired.timeoutExpired(), ActivityState::Effect::none);
  expired.signifyActivity(true, 2.0);
  expired.signifyActivity(false, 2.0);
  expired.advance();
  expired.signifyActivity(true, 2.0);
  EXPECT_EQ(expired.timeoutExpired(), ActivityState::Effect::none);
  EXPECT_EQ(expired.status(), am_active);
}

TEST(ActivityState, StopDeactivatesActiveAndTimingOutStatesIdempotently) {
  ActivityState stopped;
  EXPECT_EQ(stopped.prepareStop(), ActivityState::Effect::none);
  EXPECT_EQ(stopped.stop(), ActivityState::Effect::none);
  stopped.signifyActivity(true, 2.0);
  EXPECT_EQ(stopped.prepareStop(), ActivityState::Effect::deactivate);
  EXPECT_EQ(stopped.status(), am_active);
  EXPECT_EQ(stopped.stop(), ActivityState::Effect::deactivate);
  EXPECT_EQ(stopped.status(), am_inactive);
  EXPECT_EQ(stopped.advance(), ActivityState::Wait::signal);
  EXPECT_EQ(stopped.stop(), ActivityState::Effect::none);
  stopped.signifyActivity(true, 2.0);
  stopped.signifyActivity(false, 2.0);
  stopped.advance();
  EXPECT_EQ(stopped.prepareStop(), ActivityState::Effect::deactivate);
  EXPECT_EQ(stopped.status(), am_timing_out);
  EXPECT_EQ(stopped.stop(), ActivityState::Effect::deactivate);
  EXPECT_EQ(stopped.status(), am_inactive);
  EXPECT_EQ(stopped.advance(), ActivityState::Wait::signal);
}

TEST(ActivityState, CActivityStatusStartsInactive) {
  EXPECT_EQ(activity_status(), am_inactive);
}
