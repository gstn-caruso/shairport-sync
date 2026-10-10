#include "playback/playback_run.hpp"
#include "cancellation_wait.hpp"
#include <gtest/gtest.h>
#include <cerrno>
#include <unistd.h>
#include <condition_variable>
#include <thread>

struct StopCleanup {
  CancellationWait cancellation;
  std::mutex ordering;
  std::condition_variable changed;
  bool cleanupEntered = false, finishCleanup = false;
  PlaybackRun run;
};

static void *waitForStop(void *argument) {
  static_cast<CancellationWait *>(argument)->block();
  return nullptr;
}
static int failCreation(pthread_t *, PlaybackRun::Routine, void *) { return EAGAIN; }
static void finish(void *argument) {
  auto &cleanup = *static_cast<StopCleanup *>(argument);
  EXPECT_FALSE(cleanup.run.isActive());
  std::unique_lock lock(cleanup.ordering);
  cleanup.cleanupEntered = true;
  cleanup.changed.notify_all();
  cleanup.changed.wait(lock, [&] { return cleanup.finishCleanup; });
}
static void *waitWithCleanup(void *argument) {
  auto &cleanup = *static_cast<StopCleanup *>(argument);
  pthread_cleanup_push(finish, argument);
  waitForStop(&cleanup.cancellation);
  pthread_cleanup_pop(1);
  return nullptr;
}
static void checkConcurrentStops(StopCleanup &cleanup) {
  auto &run = cleanup.run;
  EXPECT_FALSE(run.isActive());
  EXPECT_FALSE(run.stop());
  ASSERT_EQ(run.start(waitWithCleanup, &cleanup), PlaybackRun::StartResult::started);
  cleanup.cancellation.waitForBlocked(1);
  bool firstStopped = false, secondStopped = false;
  std::thread first([&] { firstStopped = run.stop(); });
  {
    std::unique_lock lock(cleanup.ordering);
    cleanup.changed.wait(lock, [&] { return cleanup.cleanupEntered; });
  }
  std::thread second([&] { secondStopped = run.stop(); });
  {
    std::lock_guard lock(cleanup.ordering);
    cleanup.finishCleanup = true;
  }
  cleanup.changed.notify_all();
  first.join();
  second.join();
  EXPECT_TRUE(firstStopped);
  EXPECT_FALSE(secondStopped);
  EXPECT_FALSE(run.isActive());
}

static void checkRestartAndExclusiveOwnership(StopCleanup &cleanup) {
  ASSERT_NO_FATAL_FAILURE(checkConcurrentStops(cleanup));
  auto &run = cleanup.run;
  ASSERT_EQ(run.start(waitForStop, &cleanup.cancellation), PlaybackRun::StartResult::started);
  cleanup.cancellation.waitForBlocked(2);
  EXPECT_TRUE(run.isActive());
  EXPECT_EQ(run.start(waitForStop, &cleanup.cancellation), PlaybackRun::StartResult::alreadyOwned);
  EXPECT_TRUE(run.stop());
  EXPECT_FALSE(run.isActive());
  EXPECT_FALSE(run.stop());
}

TEST(PlaybackRun, InactiveRunHasNoStopToComplete) {
  PlaybackRun run;
  EXPECT_FALSE(run.isActive());
  EXPECT_FALSE(run.stop());
}

TEST(PlaybackRun, ConcurrentStopsRetireRunBeforeCleanupCompletes) {
  StopCleanup cleanup;
  checkConcurrentStops(cleanup);
}

TEST(PlaybackRun, RestartAfterConcurrentStopKeepsExclusiveOwnership) {
  StopCleanup cleanup;
  checkRestartAndExclusiveOwnership(cleanup);
}

TEST(PlaybackRun, FailedCreationAfterCompletedRunsLeavesRunInactive) {
  StopCleanup cleanup;
  ASSERT_NO_FATAL_FAILURE(checkRestartAndExclusiveOwnership(cleanup));
  auto &run = cleanup.run;
  EXPECT_EQ(run.start(waitForStop, &cleanup.cancellation, failCreation), PlaybackRun::StartResult::failed);
  EXPECT_FALSE(run.isActive());
  EXPECT_FALSE(run.stop());
}
