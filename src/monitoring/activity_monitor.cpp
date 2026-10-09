/*
 * Activity Monitor
 *
 * Contains code to run an activity flag and associated timer
 * A pthread implements a simple state machine with three states,
 * "idle", "active" and "timing out".
 *
 *
 * This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2019--2025
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/types.h>

#include "config.h"

#include "monitoring/activity_monitor.h"
#include "monitoring/activity_state.h"
#include "runtime/common.h"



int activity_monitor_running = 0;

pthread_t activity_monitor_thread;
pthread_mutex_t activity_monitor_mutex;
pthread_cond_t activity_monitor_cv;

void going_active(int block) {
  // debug(1, "activity_monitor: state transitioning to \"active\" with%s blocking", block ? "" :
  // "out");
  if (config.cmd_active_start)
    command_execute(config.cmd_active_start, "", block);


  if (config.disable_standby_mode == disable_standby_auto) {
    config.keep_dac_busy = 1;
  }
}

void going_inactive(int block) {
  // debug(1, "activity_monitor: state transitioning to \"inactive\" with%s blocking", block ? "" :
  // "out");
  if (config.cmd_active_stop)
    command_execute(config.cmd_active_stop, "", block);


  if (config.disable_standby_mode == disable_standby_auto) {
    config.keep_dac_busy = 0;
  }
}

void activity_monitor_signify_activity(int active) {
  // this could be pthread_cancelled and there is likely to be cancellation points in the
  // hooked-on procedures
  pthread_mutex_lock(&activity_monitor_mutex);
  enum activity_effect effect = activity_state_signify(activity_state_instance(), active,
                                                       config.active_state_timeout);
  pthread_mutex_unlock(&activity_monitor_mutex);
  if (effect == activity_activate)
    going_active(config.cmd_blocking);
  else if (effect == activity_deactivate)
    going_inactive(config.cmd_blocking);
  // lock the mutex again to send a signal
  pthread_mutex_lock_and_cleanup_push(&activity_monitor_mutex);
  pthread_cond_signal(&activity_monitor_cv);
  pthread_cleanup_pop(1); // release the mutex
}

void activity_thread_cleanup_handler(__attribute__((unused)) void *arg) {
  debug(3, "activity_monitor: thread exit.");
  pthread_cond_destroy(&activity_monitor_cv);
  pthread_mutex_destroy(&activity_monitor_mutex);
}

void *activity_monitor_thread_code(void *arg) {
  int rc = pthread_mutex_init(&activity_monitor_mutex, NULL);
  if (rc)
    die("activity_monitor: error %d initialising activity_monitor_mutex.", rc);

  rc = pthread_cond_init(&activity_monitor_cv, NULL);
  if (rc)
    die("activity_monitor: error %d initialising activity_monitor_cv.", rc);
  pthread_cleanup_push(activity_thread_cleanup_handler, arg);

  uint64_t sec;
  uint64_t nsec;
  struct timespec time_for_wait;

  activity_state_reset(activity_state_instance());

  pthread_mutex_lock(&activity_monitor_mutex);
  do {
    switch (activity_state_advance(activity_state_instance())) {
    case activity_wait_signal:
      pthread_cond_wait(&activity_monitor_cv, &activity_monitor_mutex);
      break;
    case activity_begin_timeout: {
      uint64_t time_to_wait_for_wakeup_ns = (uint64_t)(config.active_state_timeout * 1000000000);
      uint64_t time_of_wakeup_ns = get_realtime_in_ns() + time_to_wait_for_wakeup_ns;
      sec = time_of_wakeup_ns / 1000000000;
      nsec = time_of_wakeup_ns % 1000000000;
      time_for_wait.tv_sec = sec;
      time_for_wait.tv_nsec = nsec;
      break;
    }
    case activity_wait_deadline:
      rc = pthread_cond_timedwait(&activity_monitor_cv, &activity_monitor_mutex, &time_for_wait);
      if (rc == ETIMEDOUT &&
          activity_state_timeout_expired(activity_state_instance()) == activity_deactivate) {
        pthread_mutex_unlock(&activity_monitor_mutex);
        going_inactive(0); // don't wait for completion -- it makes no sense
        pthread_mutex_lock(&activity_monitor_mutex);
      }
      break;
    }
  } while (1);
  pthread_mutex_unlock(&activity_monitor_mutex);
  pthread_cleanup_pop(0); // should never happen
  pthread_exit(NULL);
}

enum am_state activity_status() { return activity_state_status(activity_state_instance()); }

void activity_monitor_start() {
  // debug(1,"activity_monitor_start");
  named_pthread_create(&activity_monitor_thread, NULL, activity_monitor_thread_code, NULL,
                       "activity_mon");
  activity_monitor_running = 1;
}

void activity_monitor_stop() {
  if (activity_monitor_running) {
    debug(2, "activity_monitor_stop begin. state: %d.", activity_status());
    if (activity_state_prepare_stop(activity_state_instance()) == activity_deactivate) {
      going_inactive(config.cmd_blocking);
      activity_state_stop(activity_state_instance());
    }
    pthread_cancel(activity_monitor_thread);
    pthread_join(activity_monitor_thread, NULL);
    debug(2, "activity_monitor_stop complete");
  }
}
