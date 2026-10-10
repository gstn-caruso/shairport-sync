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


class HookTranscript {
public:
  explicit HookTranscript(double idleTimeout = 0.0, bool waitForStopHook = false) {
    EXPECT_EQ(pipe(events_.data()), 0);
    EXPECT_EQ(pipe(release_.data()), 0);
    start_ = command('A', release_[0]);
    stop_ = command('D', waitForStopHook ? release_[0] : -1);
    config.cmd_active_start = start_.data();
    config.cmd_active_stop = stop_.data();
    config.cmd_blocking = 1;
    config.active_state_timeout = idleTimeout;
    activity_monitor_start();
  }

  ~HookTranscript() {
    release();
    activity_monitor_stop();
    config.cmd_active_start = nullptr;
    config.cmd_active_stop = nullptr;
    config.cmd_blocking = 0;
    while (next(0).transition != '\0') {}
    for (auto child : children_)
      waitpid(child, nullptr, 0);
    for (auto fd : events_)
      close(fd);
    for (auto fd : release_)
      close(fd);
  }

  HookEvent next(int timeout = 500) {
    pollfd ready{events_[0], POLLIN, 0};
    if (poll(&ready, 1, timeout) != 1)
      return {0, '\0'};
    HookEvent event{};
    if (read(events_[0], &event, sizeof(event)) != sizeof(event))
      return {0, '\0'};
    children_.push_back(event.child);
    return event;
  }

  void release() {
    const char wake = 'x';
    EXPECT_EQ(write(release_[1], &wake, 1), 1);
  }

private:
  std::string command(char transition, int releaseFd) {
    return "/proc/self/exe --activity-hook " + std::to_string(events_[1]) + " " +
           std::to_string(releaseFd) + " " + transition;
  }
  std::array<int, 2> events_{};
  std::array<int, 2> release_{};
  std::vector<pid_t> children_;
  std::string start_;
  std::string stop_;
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
      if (poll(&release, 1, 2000) == 1) {
        char wake;
        read(releaseFd, &wake, 1);
      }
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

static void *signifyFromCancellableCaller(void *) {
  activity_monitor_signify_activity(1);
  return nullptr;
}

TEST(ActivityMonitorLifecycle, CancelledHookCallerReleasesAdmissionAndOwnerRemainsRestartable) {
  HookTranscript hooks;
  pthread_t caller;
  ASSERT_EQ(pthread_create(&caller, nullptr, signifyFromCancellableCaller, nullptr), 0);
  ASSERT_EQ(hooks.next().transition, 'A');
  ASSERT_EQ(pthread_cancel(caller), 0);
  void *result = nullptr;
  ASSERT_EQ(pthread_join(caller, &result), 0);
  EXPECT_EQ(result, PTHREAD_CANCELED);
  hooks.release();
  activity_monitor_stop();
  EXPECT_EQ(hooks.next().transition, 'D');
  EXPECT_EQ(activity_status(), am_inactive);
  activity_monitor_start();
  hooks.release();
  activity_monitor_signify_activity(1);
  EXPECT_EQ(hooks.next().transition, 'A');
  activity_monitor_stop();
  EXPECT_EQ(hooks.next().transition, 'D');
}

TEST(ActivityMonitorLifecycle, StopWaitsForAdmittedHookWithoutBlockingStatusOrAllowingLateActivation) {
  HookTranscript hooks;
  auto activation = std::async(std::launch::async, [] { activity_monitor_signify_activity(1); });
  ASSERT_EQ(hooks.next().transition, 'A');
  auto status = std::async(std::launch::async, [] { return activity_status(); });
  ASSERT_EQ(status.wait_for(100ms), std::future_status::ready);
  EXPECT_EQ(status.get(), am_active);
  auto stop = std::async(std::launch::async, [] { activity_monitor_stop(); });
  EXPECT_EQ(stop.wait_for(20ms), std::future_status::timeout);
  auto lateActivation = std::async(std::launch::async, [] { activity_monitor_signify_activity(1); });
  hooks.release();
  EXPECT_EQ(activation.wait_for(500ms), std::future_status::ready);
  EXPECT_EQ(stop.wait_for(500ms), std::future_status::ready);
  EXPECT_EQ(lateActivation.wait_for(500ms), std::future_status::ready);
  EXPECT_EQ(hooks.next().transition, 'D');
  EXPECT_EQ(hooks.next(50).transition, '\0');
  EXPECT_EQ(activity_status(), am_inactive);
}

TEST(ActivityMonitorLifecycle, DuplicateLifecycleAndPendingDeadlineDeactivateExactlyOnce) {
  HookTranscript hooks(0.1);
  hooks.release();
  activity_monitor_signify_activity(1);
  EXPECT_EQ(hooks.next().transition, 'A');
  activity_monitor_start();
  activity_monitor_signify_activity(0);
  ASSERT_TRUE(reachesStatus(am_timing_out));
  activity_monitor_stop();
  activity_monitor_stop();
  EXPECT_EQ(hooks.next().transition, 'D');
  EXPECT_EQ(hooks.next(150).transition, '\0');
  EXPECT_EQ(activity_status(), am_inactive);
}

