#include "monitoring/activity_monitor.h"
#include "runtime/common.h"
#include <gtest/gtest.h>

TEST(ActivityMonitorLifecycle, RejectsSignalsBeforeStart) {
  activity_monitor_stop();
  activity_monitor_signify_activity(7);
  EXPECT_EQ(activity_status(), am_inactive);
}
