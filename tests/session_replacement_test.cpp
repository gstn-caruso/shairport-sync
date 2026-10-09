#include "runtime_principal_session.hpp"
#include <cassert>
#include <condition_variable>

static RuntimePrincipalSession principal;
static SessionState first{}, second{};
static std::mutex ordering;
static std::condition_variable changed;
static bool firstPaused, secondSelected, firstRejected;

static void *resumeFirst(void *) {
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  {
    std::unique_lock lock(ordering);
    firstPaused = true;
    changed.notify_all();
    changed.wait(lock, [] { return secondSelected; });
  }
  auto acquisition = principal.acquire(first, true);
  if (acquisition.accepted) {
    pthread_cancel(second.thread);
    pthread_join(second.thread, nullptr);
  }
  firstRejected = !acquisition.accepted;
  pthread_setcancelstate(previousState, nullptr);
  pthread_testcancel();
  return nullptr;
}

static void *replaceFirst(void *) {
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  {
    std::unique_lock lock(ordering);
    changed.wait(lock, [] { return firstPaused; });
  }
  auto acquisition = principal.acquire(second, true);
  assert(acquisition.accepted && acquisition.previousId == 1);
  pthread_cancel(first.thread);
  {
    std::lock_guard lock(ordering);
    secondSelected = true;
  }
  changed.notify_all();
  assert(pthread_join(first.thread, nullptr) == 0);
  pthread_setcancelstate(previousState, nullptr);
  pthread_testcancel();
  return nullptr;
}

int main() {
  first.connection_number = 1;
  second.connection_number = 2;
  assert(principal.acquire(first, true).accepted);
  assert(pthread_create(&first.thread, nullptr, resumeFirst, nullptr) == 0);
  assert(pthread_create(&second.thread, nullptr, replaceFirst, nullptr) == 0);
  assert(pthread_join(second.thread, nullptr) == 0);
  assert(firstRejected);
  assert(principal.isCurrent(2));
}
