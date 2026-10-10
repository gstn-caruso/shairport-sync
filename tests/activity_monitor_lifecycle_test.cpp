#include "monitoring/activity_monitor.h"
#include "runtime/common.h"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <array>
#include <atomic>
#include <cstdlib>
#include <future>
#include <poll.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace std::chrono_literals;

struct HookEvent {
  pid_t child;
  char transition;
};


static int exitEventFd;

static void observeExplicitExitStop() {
  HookEvent event{getpid(), activity_status() == am_active ? 'A' : 'D'};
  write(exitEventFd, &event, sizeof(event));
  activity_monitor_stop();
}

int main(int argc, char **argv) {
  if (argc == 5 && std::string(argv[1]) == "--activity-hook") {
    const int eventFd = std::atoi(argv[2]);
    const int releaseFd = std::atoi(argv[3]);
    HookEvent event{getpid(), argv[4][0]};
    if (write(eventFd, &event, sizeof(event)) != sizeof(event))
      return 1;
    if (releaseFd >= 0) {
      pollfd release{releaseFd, POLLIN, 0};
      poll(&release, 1, 2000);
    }
    return 0;
  }
  if (argc == 3 && std::string(argv[1]) == "--activity-exit") {
    exitEventFd = std::atoi(argv[2]);
    std::atexit(observeExplicitExitStop);
    activity_monitor_start();
    activity_monitor_signify_activity(1);
    return 0;
  }
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

static bool reachesStatus(am_state expected, std::chrono::milliseconds limit = 500ms) {
  const auto deadline = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < deadline) {
    if (activity_status() == expected)
      return true;
    std::this_thread::sleep_for(1ms);
  }
  return activity_status() == expected;
}

TEST(ActivityMonitorLifecycle, RejectsSignalsBeforeStart) {
  activity_monitor_stop();
  activity_monitor_signify_activity(7);
  EXPECT_EQ(activity_status(), am_inactive);
}

TEST(ActivityMonitorLifecycle, OwnerOutlivesExplicitExitStop) {
  std::array<int, 2> events;
  ASSERT_EQ(pipe(events.data()), 0);
  const auto eventFd = std::to_string(events[1]);
  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    execl("/proc/self/exe", "activity-monitor-test", "--activity-exit", eventFd.c_str(), nullptr);
    _exit(1);
  }
  pollfd ready{events[0], POLLIN, 0};
  ASSERT_EQ(poll(&ready, 1, 500), 1);
  HookEvent event{};
  EXPECT_EQ(read(events[0], &event, sizeof(event)), sizeof(event));
  EXPECT_EQ(event.transition, 'A');
  int result;
  EXPECT_EQ(waitpid(child, &result, 0), child);
  EXPECT_EQ(result, 0);
  close(events[0]);
  close(events[1]);
}

TEST(ActivityMonitorLifecycle, IdleWorkerStopsPromptly) {
  activity_monitor_start();
  const auto before = std::chrono::steady_clock::now();
  activity_monitor_stop();
  EXPECT_LT(std::chrono::steady_clock::now() - before, 500ms);
  EXPECT_EQ(activity_status(), am_inactive);
}

TEST(ActivityMonitorLifecycle, RealWorkerExpiresDelayedInactivity) {
  config.disable_standby_mode = disable_standby_auto;
  config.active_state_timeout = 0.03;
  activity_monitor_start();
  activity_monitor_signify_activity(1);
  activity_monitor_signify_activity(0);
  ASSERT_TRUE(reachesStatus(am_timing_out));
  ASSERT_TRUE(reachesStatus(am_inactive));
  activity_monitor_stop();
  EXPECT_EQ(config.keep_dac_busy, 0);
}

TEST(ActivityMonitorLifecycle, ReactivationCancelsDelayedInactivity) {
  config.active_state_timeout = 0.03;
  activity_monitor_start();
  activity_monitor_signify_activity(1);
  activity_monitor_signify_activity(0);
  ASSERT_TRUE(reachesStatus(am_timing_out));
  activity_monitor_signify_activity(1);
  ASSERT_TRUE(reachesStatus(am_active));
  std::this_thread::sleep_for(60ms);
  EXPECT_EQ(activity_status(), am_active);
  activity_monitor_stop();
}

TEST(ActivityMonitorLifecycle, StopsPendingDeadlineWithoutWaitingForExpiry) {
  config.active_state_timeout = 1.0;
  activity_monitor_start();
  activity_monitor_signify_activity(1);
  activity_monitor_signify_activity(0);
  ASSERT_TRUE(reachesStatus(am_timing_out));
  const auto before = std::chrono::steady_clock::now();
  activity_monitor_stop();
  EXPECT_LT(std::chrono::steady_clock::now() - before, 500ms);
  EXPECT_EQ(activity_status(), am_inactive);
}

TEST(ActivityMonitorLifecycle, DuplicateStartAndStopPreserveActiveStateAndRestart) {
  config.active_state_timeout = 0.0;
  activity_monitor_start();
  activity_monitor_signify_activity(1);
  activity_monitor_start();
  EXPECT_EQ(activity_status(), am_active);
  activity_monitor_stop();
  activity_monitor_stop();
  activity_monitor_start();
  EXPECT_EQ(activity_status(), am_inactive);
  activity_monitor_signify_activity(1);
  EXPECT_EQ(activity_status(), am_active);
  activity_monitor_stop();
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
