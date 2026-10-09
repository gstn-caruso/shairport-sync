#include "activity_monitor.h"
#include "common.h"
#include <assert.h>

extern pthread_mutex_t activity_monitor_mutex;
extern pthread_cond_t activity_monitor_cv;

int main(void) {
  assert(pthread_mutex_init(&activity_monitor_mutex, NULL) == 0);
  assert(pthread_cond_init(&activity_monitor_cv, NULL) == 0);
  config.disable_standby_mode = disable_standby_auto;
  config.active_state_timeout = 0.0;
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
  assert(activity_status() == am_active);
  assert(config.keep_dac_busy == 1);
  assert(pthread_cond_destroy(&activity_monitor_cv) == 0);
  assert(pthread_mutex_destroy(&activity_monitor_mutex) == 0);
}
