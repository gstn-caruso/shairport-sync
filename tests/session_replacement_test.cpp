#include "runtime_principal_session.hpp"
#include <gtest/gtest.h>
#include <cassert>
#include <condition_variable>

struct ReplacementState {
  RuntimePrincipalSession principal;
  SessionState first{}, second{};
  std::mutex ordering;
  std::condition_variable changed;
  bool firstPaused = false, secondSelected = false, firstRejected = false;
};

static void *resumeFirst(void *argument) {
  auto &state = *static_cast<ReplacementState *>(argument);
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  {
    std::unique_lock lock(state.ordering);
    state.firstPaused = true;
    state.changed.notify_all();
    state.changed.wait(lock, [&] { return state.secondSelected; });
  }
  auto acquisition = state.principal.acquire(state.first, true);
  if (acquisition.accepted) {
    pthread_cancel(state.second.thread);
    pthread_join(state.second.thread, nullptr);
  }
  state.firstRejected = !acquisition.accepted;
  pthread_setcancelstate(previousState, nullptr);
  pthread_testcancel();
  return nullptr;
}

static void *replaceFirst(void *argument) {
  auto &state = *static_cast<ReplacementState *>(argument);
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  {
    std::unique_lock lock(state.ordering);
    state.changed.wait(lock, [&] { return state.firstPaused; });
  }
  auto acquisition = state.principal.acquire(state.second, true);
  assert(acquisition.accepted && acquisition.previousId == 1);
  pthread_cancel(state.first.thread);
  {
    std::lock_guard lock(state.ordering);
    state.secondSelected = true;
  }
  state.changed.notify_all();
  assert(pthread_join(state.first.thread, nullptr) == 0);
  pthread_setcancelstate(previousState, nullptr);
  pthread_testcancel();
  return nullptr;
}

TEST(SessionReplacement, DisplacedSessionCannotReacquirePrincipalAndCreateMutualJoin) {
  ReplacementState state;
  state.first.connection_number = 1;
  state.second.connection_number = 2;
  assert(state.principal.acquire(state.first, true).accepted);
  assert(pthread_create(&state.first.thread, nullptr, resumeFirst, &state) == 0);
  assert(pthread_create(&state.second.thread, nullptr, replaceFirst, &state) == 0);
  assert(pthread_join(state.second.thread, nullptr) == 0);
  EXPECT_TRUE(state.firstRejected);
  EXPECT_TRUE(state.principal.isCurrent(2));
}
