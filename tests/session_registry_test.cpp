#include "session_registry.hpp"
#include "runtime_principal_session.hpp"
#include <cassert>
#include <cerrno>
#include <condition_variable>
#include <sys/socket.h>
#include <unistd.h>

static int rejectThread(pthread_t *, void *(*)(void *), void *) { return EAGAIN; }
static void *unusedThread(void *) { assert(false); return nullptr; }
static SessionRegistry *activeRegistry;
static std::mutex completionMutex;
static std::condition_variable completionChanged;
static bool finished;
static RuntimePrincipalSession *activePrincipal;
static bool cleanupEntered;
static int cleanupCount;
static bool permitCleanup;
static bool cleanupFinished;
static void finishCancelled(void *argument) {
  auto *session = static_cast<SessionState *>(argument);
  {
    std::unique_lock lock(completionMutex);
    cleanupEntered = true;
    ++cleanupCount;
    completionChanged.notify_all();
    completionChanged.wait(lock, [] { return permitCleanup; });
  }
  activePrincipal->releaseIfCurrent(session->connection_number);
  activeRegistry->markFinished(session->connection_number);
  {
    std::lock_guard lock(completionMutex);
    cleanupFinished = true;
  }
  completionChanged.notify_all();
}
static void *waitForCancellation(void *argument) {
  auto *session = static_cast<SessionState *>(argument);
  pthread_cleanup_push(finishCancelled, argument);
  char byte;
  read(session->fd, &byte, 1);
  pthread_cleanup_pop(1);
  return nullptr;
}
static void *retireSession(void *) {
  assert(activeRegistry->cancelAndJoin(5));
  return nullptr;
}
static void *retireAll(void *) {
  activeRegistry->cancelAndJoinMatching(unspecified_stream_category, 0);
  return nullptr;
}
static void *finishImmediately(void *argument) {
  auto *session = static_cast<SessionState *>(argument);
  activeRegistry->markFinished(session->connection_number);
  {
    std::lock_guard lock(completionMutex);
    finished = true;
  }
  completionChanged.notify_one();
  return nullptr;
}

int main() {
  int sockets[2];
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  auto session = std::make_unique<SessionState>();
  session->connection_number = 1;
  session->fd = sockets[0];
  SessionRegistry registry(rejectThread);
  assert(registry.start(std::move(session), unusedThread) == EAGAIN);
  assert(!registry.takeById(1));
  char byte;
  assert(read(sockets[1], &byte, 1) == 0);
  close(sockets[1]);
  SessionRegistry successful;
  activeRegistry = &successful;
  auto immediate = std::make_unique<SessionState>();
  immediate->connection_number = 2;
  assert(successful.start(std::move(immediate), finishImmediately) == 0);
  {
    std::unique_lock lock(completionMutex);
    completionChanged.wait(lock, [] { return finished; });
  }
  auto completed = successful.takeFinished();
  assert(completed.size() == 1);
  assert(successful.takeFinished().empty());
  assert(!successful.takeById(2));
  assert(pthread_join(completed[0]->thread, nullptr) == 0);
  RuntimePrincipalSession principal;
  SessionState first{}, replacement{};
  first.connection_number = 3;
  replacement.connection_number = 4;
  assert(principal.acquire(first, false).accepted);
  assert(!principal.acquire(replacement, false).accepted);
  auto changed = principal.acquire(replacement, true);
  assert(changed.accepted && changed.previousId == 3);
  assert(!principal.releaseIfCurrent(3));
  assert(principal.isCurrent(4));
  assert(principal.snapshot().id == 4);
  assert(principal.clear() == 4);
  assert(!principal.isCurrent(4));
  SessionRegistry cancellation;
  activeRegistry = &cancellation;
  activePrincipal = &principal;
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  auto cancellable = std::make_unique<SessionState>();
  cancellable->connection_number = 5;
  cancellable->fd = sockets[0];
  assert(principal.acquire(*cancellable, false).accepted);
  assert(cancellation.start(std::move(cancellable), waitForCancellation) == 0);
  pthread_t retiring;
  assert(pthread_create(&retiring, nullptr, retireSession, nullptr) == 0);
  {
    std::unique_lock lock(completionMutex);
    completionChanged.wait(lock, [] { return cleanupEntered; });
  }
  assert(!cancellation.cancelAndJoin(5));
  assert(pthread_cancel(retiring) == 0);
  assert(recv(sockets[1], &byte, 1, MSG_DONTWAIT) == -1 && errno == EAGAIN);
  {
    std::lock_guard lock(completionMutex);
    permitCleanup = true;
  }
  completionChanged.notify_all();
  void *result;
  assert(pthread_join(retiring, &result) == 0);
  assert(result == PTHREAD_CANCELED);
  assert(cleanupFinished);
  assert(!principal.isCurrent(5));
  assert(cancellation.takeFinished().empty());
  assert(read(sockets[1], &byte, 1) == 0);
  close(sockets[1]);
  permitCleanup = false;
  cleanupCount = 0;
  int secondSockets[2];
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, secondSockets) == 0);
  auto one = std::make_unique<SessionState>();
  auto two = std::make_unique<SessionState>();
  one->connection_number = 6;
  two->connection_number = 7;
  one->fd = sockets[0];
  two->fd = secondSockets[0];
  assert(cancellation.start(std::move(one), waitForCancellation) == 0);
  assert(cancellation.start(std::move(two), waitForCancellation) == 0);
  assert(pthread_create(&retiring, nullptr, retireAll, nullptr) == 0);
  {
    std::unique_lock lock(completionMutex);
    completionChanged.wait(lock, [] { return cleanupCount == 2; });
    permitCleanup = true;
  }
  completionChanged.notify_all();
  assert(pthread_join(retiring, nullptr) == 0);
  assert(read(sockets[1], &byte, 1) == 0);
  assert(read(secondSockets[1], &byte, 1) == 0);
  close(sockets[1]);
  close(secondSockets[1]);
}
