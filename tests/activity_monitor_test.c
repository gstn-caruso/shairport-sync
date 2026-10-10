#include "monitoring/activity_monitor.h"
#include "runtime/common.h"
#include <assert.h>

int main(void) {
  activity_monitor_stop();
  config.disable_standby_mode = disable_standby_auto;
  config.active_state_timeout = 0.0;
  activity_monitor_start();
  assert(activity_status() == am_inactive);
  activity_monitor_signify_activity(7);
  assert(activity_status() == am_active);
  assert(config.keep_dac_busy == 1);
  activity_monitor_signify_activity(1);
  assert(activity_status() == am_active);
  activity_monitor_signify_activity(0);
  assert(activity_status() == am_inactive);
  assert(config.keep_dac_busy == 0);
  config.active_state_timeout = 2.0;
  activity_monitor_signify_activity(1);
  activity_monitor_signify_activity(0);
  enum am_state delayed_status = activity_status();
  assert(delayed_status == am_active || delayed_status == am_timing_out);
  assert(config.keep_dac_busy == 1);
  activity_monitor_stop();
}
