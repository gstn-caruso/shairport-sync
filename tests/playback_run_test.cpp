#include "playback_run.hpp"
#include "cancellation_wait.hpp"
#include <cassert>
#include <cerrno>
#include <unistd.h>
#include <condition_variable>
#include <thread>

static CancellationWait cancellation;
static void *waitForStop(void *) {
  cancellation.block();
  return nullptr;
}
static int failCreation(pthread_t *, PlaybackRun::Routine, void *) { return EAGAIN; }
static std::mutex ordering;
static std::condition_variable changed;
static bool cleanupEntered = false, finishCleanup = false;
static PlaybackRun *activeRun;
static void finish(void *) {
  assert(!activeRun->isActive());
  std::unique_lock lock(ordering);
  cleanupEntered = true;
  changed.notify_all();
  changed.wait(lock, [] { return finishCleanup; });
}
static void *waitWithCleanup(void *) {
  pthread_cleanup_push(finish, nullptr);
  waitForStop(nullptr);
  pthread_cleanup_pop(1);
  return nullptr;
}
int main() {
  PlaybackRun run;
  assert(!run.isActive() && !run.stop());
  activeRun = &run;
  assert(run.start(waitWithCleanup, nullptr) == PlaybackRun::StartResult::started);
  cancellation.waitForBlocked(1);
  bool firstStopped = false, secondStopped = false;
  std::thread first([&] { firstStopped = run.stop(); });
  {
    std::unique_lock lock(ordering);
    changed.wait(lock, [] { return cleanupEntered; });
  }
  std::thread second([&] { secondStopped = run.stop(); });
  {
    std::lock_guard lock(ordering);
    finishCleanup = true;
  }
  changed.notify_all();
  first.join();
  second.join();
  assert(firstStopped && !secondStopped && !run.isActive());
  assert(run.start(waitForStop, nullptr) == PlaybackRun::StartResult::started);
  cancellation.waitForBlocked(2);
  assert(run.isActive());
  assert(run.start(waitForStop, nullptr) == PlaybackRun::StartResult::alreadyOwned);
  assert(run.stop() && !run.isActive());
  assert(!run.stop());
  assert(run.start(waitForStop, nullptr, failCreation) == PlaybackRun::StartResult::failed);
  assert(!run.isActive() && !run.stop());
}
