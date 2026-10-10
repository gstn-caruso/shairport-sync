#include "session/runtime_principal_session.hpp"
#include <gtest/gtest.h>
#include <condition_variable>

struct ReplacementState {
  RuntimePrincipalSession principal;
  SessionState first{}, second{};
  std::mutex ordering;
  std::condition_variable changed;
  bool firstPaused = false, secondSelected = false, firstRejected = false;
  bool setupAborted = false;
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
    if (state.setupAborted) {
      pthread_setcancelstate(previousState, nullptr);
      return nullptr;
    }
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
  EXPECT_TRUE(acquisition.accepted);
  EXPECT_EQ(acquisition.previousId, 1);
  pthread_cancel(state.first.thread);
  {
    std::lock_guard lock(state.ordering);
    state.secondSelected = true;
  }
  state.changed.notify_all();
  EXPECT_EQ(pthread_join(state.first.thread, nullptr), 0);
  pthread_setcancelstate(previousState, nullptr);
  pthread_testcancel();
  return nullptr;
}

TEST(SessionReplacement, DisplacedSessionCannotReacquirePrincipalAndCreateMutualJoin) {
  ReplacementState state;
  state.first.connection_number = 1;
  state.second.connection_number = 2;
  ASSERT_TRUE(state.principal.acquire(state.first, true).accepted);
  ASSERT_EQ(pthread_create(&state.first.thread, nullptr, resumeFirst, &state), 0);
  const auto replacementStarted = pthread_create(&state.second.thread, nullptr, replaceFirst, &state);
  if (replacementStarted != 0) {
    {
      std::lock_guard lock(state.ordering);
      state.setupAborted = true;
      state.secondSelected = true;
    }
    state.changed.notify_all();
    EXPECT_EQ(pthread_join(state.first.thread, nullptr), 0);
  }
  ASSERT_EQ(replacementStarted, 0);
  EXPECT_EQ(pthread_join(state.second.thread, nullptr), 0);
  EXPECT_TRUE(state.firstRejected);
  EXPECT_TRUE(state.principal.isCurrent(2));
}
