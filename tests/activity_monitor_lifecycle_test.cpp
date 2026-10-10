#include "monitoring/activity_monitor.h"
#include "runtime/common.h"
#include <gtest/gtest.h>
#include <chrono>

using namespace std::chrono_literals;

TEST(ActivityMonitorLifecycle, RejectsSignalsBeforeStart) {
  activity_monitor_stop();
  activity_monitor_signify_activity(7);
  EXPECT_EQ(activity_status(), am_inactive);
}

TEST(ActivityMonitorLifecycle, StopClosesAdmissionAndCanRestart) {
  config.disable_standby_mode = disable_standby_auto;
  config.active_state_timeout = 0.0;
  activity_monitor_start();
  activity_monitor_signify_activity(7);
  EXPECT_EQ(activity_status(), am_active);
  EXPECT_EQ(config.keep_dac_busy, 1);
  const auto before = std::chrono::steady_clock::now();
  activity_monitor_stop();
  EXPECT_LT(std::chrono::steady_clock::now() - before, 500ms);
  EXPECT_EQ(activity_status(), am_inactive);
  EXPECT_EQ(config.keep_dac_busy, 0);
  activity_monitor_signify_activity(1);
  EXPECT_EQ(activity_status(), am_inactive);
  activity_monitor_start();
  activity_monitor_signify_activity(1);
  EXPECT_EQ(activity_status(), am_active);
  activity_monitor_signify_activity(0);
  EXPECT_EQ(activity_status(), am_inactive);
  EXPECT_EQ(config.keep_dac_busy, 0);
  activity_monitor_stop();
}
