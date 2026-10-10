/*
 * Activity Monitor
 *
 * Contains code to run an activity flag and associated timer
 * A cooperative worker implements a simple state machine with three states,
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

#include "monitoring/activity_monitor.hpp"
#include "runtime/common.h"

ActivityMonitor::~ActivityMonitor() { stop(); }

void ActivityMonitor::start() {
  std::lock_guard lifecycle(lifecycleMutex_);
  std::lock_guard state(stateMutex_);
  if (running_)
    return;
  activity_ = ActivityState{};
  running_ = true;
  worker_ = std::thread(&ActivityMonitor::waitForInactivity, this);
}

void ActivityMonitor::stop() {
  std::lock_guard lifecycle(lifecycleMutex_);
  {
    std::lock_guard state(stateMutex_);
    if (!running_)
      return;
    running_ = false;
    changed_.notify_all();
  }
  worker_.join();
  std::lock_guard effects(effectsMutex_);
  ActivityState::Effect effect;
  {
    std::lock_guard state(stateMutex_);
    effect = activity_.stop();
  }
  applyEffect(effect, config.cmd_blocking);
}

am_state ActivityMonitor::status() {
  std::lock_guard state(stateMutex_);
  return activity_.status();
}

void ActivityMonitor::signifyActivity(bool active) {
  std::lock_guard effects(effectsMutex_);
  ActivityState::Effect effect;
  {
    std::lock_guard state(stateMutex_);
    if (!running_)
      return;
    effect = activity_.signifyActivity(active, config.active_state_timeout);
    changed_.notify_all();
  }
  applyEffect(effect, config.cmd_blocking);
}

void ActivityMonitor::applyEffect(ActivityState::Effect effect, int blocking) {
  if (effect == ActivityState::Effect::none)
    return;
  const auto command = effect == ActivityState::Effect::activate ? config.cmd_active_start
                                                               : config.cmd_active_stop;
  if (command)
    command_execute(command, "", blocking);
  std::lock_guard state(stateMutex_);
  if (config.disable_standby_mode == disable_standby_auto)
    config.keep_dac_busy = effect == ActivityState::Effect::activate ? 1 : 0;
}

void ActivityMonitor::waitForInactivity() {
  std::unique_lock state(stateMutex_);
  while (running_) {
    switch (activity_.advance()) {
    case ActivityState::Wait::signal:
      changed_.wait(state);
      break;
    case ActivityState::Wait::beginTimeout:
      deadline_ = std::chrono::steady_clock::now() +
                  std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                      std::chrono::duration<double>(config.active_state_timeout));
      break;
    case ActivityState::Wait::deadline:
      if (changed_.wait_until(state, deadline_) == std::cv_status::timeout) {
        state.unlock();
        {
          std::lock_guard effects(effectsMutex_);
          ActivityState::Effect effect = ActivityState::Effect::none;
          {
            std::lock_guard check(stateMutex_);
            if (running_)
              effect = activity_.timeoutExpired();
          }
          applyEffect(effect, 0);
        }
        state.lock();
      }
      break;
    }
  }
}

static ActivityMonitor monitor;

void activity_monitor_start() { monitor.start(); }
void activity_monitor_stop() { monitor.stop(); }
void activity_monitor_signify_activity(int active) {
  monitor.signifyActivity(active != 0);
}
am_state activity_status() { return monitor.status(); }
