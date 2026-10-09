#include "session_registry.hpp"
#include "runtime_principal_session.hpp"
#include "cancellation_wait.hpp"
#include <gtest/gtest.h>
#include <cassert>
#include <cerrno>
#include <condition_variable>
#include <sys/socket.h>
#include <unistd.h>

template <typename Registry> concept ExposesTakeById = requires(Registry &registry) {
  registry.takeById(1);
};
template <typename Registry> concept ExposesTakeFinished = requires(Registry &registry) {
  registry.takeFinished();
};
template <typename Registry> concept ExposesTakeMatching = requires(Registry &registry) {
  registry.takeMatching(unspecified_stream_category, 0);
};
static_assert(!ExposesTakeById<SessionRegistry>);
static_assert(!ExposesTakeFinished<SessionRegistry>);
static_assert(!ExposesTakeMatching<SessionRegistry>);

static int rejectThread(pthread_t *, void *(*)(void *), void *) { return EAGAIN; }
static void *unusedThread(void *) { assert(false); return nullptr; }
struct RegistryScenario {
  SessionRegistry *activeRegistry = nullptr;
  RuntimePrincipalSession *activePrincipal = nullptr;
  std::mutex completionMutex;
  std::condition_variable completionChanged;
  bool finished = false, cleanupEntered = false;
  int cleanupCount = 0;
  bool permitCleanup = false, cleanupFinished = false;
  CancellationWait cancellation;
};
static RegistryScenario *scenario;
static void finishCancelled(void *argument) {
  auto &current = *scenario;
  auto *session = static_cast<SessionState *>(argument);
  {
    std::unique_lock lock(current.completionMutex);
    current.cleanupEntered = true;
    ++current.cleanupCount;
    current.completionChanged.notify_all();
    current.completionChanged.wait(lock, [&] { return current.permitCleanup; });
  }
  current.activePrincipal->releaseIfCurrent(session->connection_number);
  current.activeRegistry->markFinished(session->connection_number);
  {
    std::lock_guard lock(current.completionMutex);
    current.cleanupFinished = true;
  }
  current.completionChanged.notify_all();
}
static void *waitForCancellation(void *argument) {
  pthread_cleanup_push(finishCancelled, argument);
  scenario->cancellation.block();
  pthread_cleanup_pop(1);
  return nullptr;
}
static void *retireSession(void *) {
  assert(scenario->activeRegistry->cancelAndJoin(5));
  return nullptr;
}
static void *retireAll(void *) {
  scenario->activeRegistry->cancelAndJoinMatching(unspecified_stream_category, 0);
  return nullptr;
}
static void *finishImmediately(void *argument) {
  auto &current = *scenario;
  auto *session = static_cast<SessionState *>(argument);
  current.activeRegistry->markFinished(session->connection_number);
  {
    std::lock_guard lock(current.completionMutex);
    current.finished = true;
  }
  current.completionChanged.notify_one();
  return nullptr;
}

static void withScenario(void (*check)(RegistryScenario &)) {
  const auto savedScenario = scenario;
  RegistryScenario current;
  scenario = &current;
  check(current);
  scenario = savedScenario;
}

static void checkFailedThreadCreation() {
  int sockets[2];
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  auto session = std::make_unique<SessionState>();
  session->connection_number = 1;
  session->fd = sockets[0];
  SessionRegistry registry(rejectThread);
  assert(registry.start(std::move(session), unusedThread) == EAGAIN);
  assert(!registry.cancelAndJoin(1));
  char byte;
  assert(read(sockets[1], &byte, 1) == 0);
  close(sockets[1]);
}

static void checkImmediateCompletion(RegistryScenario &current) {
  int sockets[2];
  char byte;
  SessionRegistry successful;
  current.activeRegistry = &successful;
  auto immediate = std::make_unique<SessionState>();
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  immediate->fd = sockets[0];
  immediate->connection_number = 2;
  assert(successful.start(std::move(immediate), finishImmediately) == 0);
  {
    std::unique_lock lock(current.completionMutex);
    current.completionChanged.wait(lock, [&] { return current.finished; });
  }
  successful.joinFinished();
  successful.joinFinished();
  assert(!successful.cancelAndJoin(2));
  assert(read(sockets[1], &byte, 1) == 0);
  close(sockets[1]);
}

static void checkPrincipalReplacement(RuntimePrincipalSession &principal) {
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
}

static void checkCancelledRetirement(RegistryScenario &current, SessionRegistry &cancellation,
                                     RuntimePrincipalSession &principal) {
  checkPrincipalReplacement(principal);
  int sockets[2];
  char byte;
  current.activeRegistry = &cancellation;
  current.activePrincipal = &principal;
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  auto cancellable = std::make_unique<SessionState>();
  cancellable->connection_number = 5;
  cancellable->fd = sockets[0];
  assert(principal.acquire(*cancellable, false).accepted);
  assert(cancellation.start(std::move(cancellable), waitForCancellation) == 0);
  pthread_t retiring;
  assert(pthread_create(&retiring, nullptr, retireSession, nullptr) == 0);
  {
    std::unique_lock lock(current.completionMutex);
    current.completionChanged.wait(lock, [&] { return current.cleanupEntered; });
  }
  assert(!cancellation.cancelAndJoin(5));
  assert(pthread_cancel(retiring) == 0);
  assert(recv(sockets[1], &byte, 1, MSG_DONTWAIT) == -1 && errno == EAGAIN);
  {
    std::lock_guard lock(current.completionMutex);
    current.permitCleanup = true;
  }
  current.completionChanged.notify_all();
  void *result;
  assert(pthread_join(retiring, &result) == 0);
  assert(result == PTHREAD_CANCELED);
  assert(current.cleanupFinished);
  assert(!principal.isCurrent(5));
  cancellation.joinFinished();
  assert(!cancellation.cancelAndJoin(5));
  assert(read(sockets[1], &byte, 1) == 0);
  close(sockets[1]);
}

static void checkRetirementScenario(RegistryScenario &current) {
  RuntimePrincipalSession principal;
  SessionRegistry cancellation;
  checkCancelledRetirement(current, cancellation, principal);
}

static void checkBatchRetirement(RegistryScenario &current) {
  RuntimePrincipalSession principal;
  SessionRegistry cancellation;
  checkCancelledRetirement(current, cancellation, principal);
  {
    std::lock_guard lock(current.completionMutex);
    current.permitCleanup = false;
    current.cleanupCount = 0;
  }
  int sockets[2];
  char byte;
  pthread_t retiring;
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
    std::unique_lock lock(current.completionMutex);
    current.completionChanged.wait(lock, [&] { return current.cleanupCount == 2; });
    current.permitCleanup = true;
  }
  current.completionChanged.notify_all();
  assert(pthread_join(retiring, nullptr) == 0);
  assert(read(sockets[1], &byte, 1) == 0);
  assert(read(secondSockets[1], &byte, 1) == 0);
  close(sockets[1]);
  close(secondSockets[1]);
}

TEST(SessionRegistry, FailedThreadCreationClosesOwnedSocket) {
  checkFailedThreadCreation();
}

TEST(SessionRegistry, ImmediateCompletionIsRetainedForOneJoin) {
  withScenario(checkImmediateCompletion);
}

TEST(SessionRegistry, ReplacementPreservesNewPrincipalUntilExplicitClear) {
  RuntimePrincipalSession principal;
  checkPrincipalReplacement(principal);
}

TEST(SessionRegistry, CallerCancellationDuringRetirementKeepsSessionOwnedUntilWorkerJoin) {
  withScenario(checkRetirementScenario);
}

TEST(SessionRegistry, BatchCancellationSignalsAllWorkersBeforeJoiningBlockedCleanup) {
  withScenario(checkBatchRetirement);
}
