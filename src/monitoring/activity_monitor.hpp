#pragma once

#include "monitoring/activity_state.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

class ActivityMonitor {
public:
  ~ActivityMonitor();
  void start();
  void stop();
  void signifyActivity(bool active);
  am_state status();

private:
  void waitForInactivity();
  void applyEffect(ActivityState::Effect effect, int blocking);

  std::mutex lifecycleMutex_;
  std::mutex effectsMutex_;
  std::mutex stateMutex_;
  std::condition_variable changed_;
  ActivityState activity_;
  std::thread worker_;
  bool running_ = false;
  std::chrono::steady_clock::time_point deadline_;
};
