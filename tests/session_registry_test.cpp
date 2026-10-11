#include "session/session_registry.hpp"
#include "session/runtime_session_worker.hpp"
#include "session/runtime_principal_session.hpp"
#include "cancellation_wait.hpp"
#include <gtest/gtest.h>
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
static void *unusedThread(void *) {
  ADD_FAILURE() << "Rejected thread creation must not execute the worker";
  return nullptr;
}
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
  EXPECT_TRUE(scenario->activeRegistry->cancelAndJoin(5));
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

class RegistryContract : public testing::Test {
  RegistryScenario *savedScenario = scenario;
protected:
  RegistryScenario current;
  RegistryContract() { scenario = &current; }
  ~RegistryContract() override { scenario = savedScenario; }
};

TEST(SessionRegistry, FailedThreadCreationClosesOwnedSocket) {
  int sockets[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  auto session = std::make_unique<SessionState>();
  session->connection_number = 1;
  session->fd = sockets[0];
  SessionRegistry registry;
  EXPECT_EQ(registry.start(std::make_unique<RuntimeSessionWorker>(
                std::move(session), unusedThread, rejectThread)), EAGAIN);
  EXPECT_FALSE(registry.cancelAndJoin(1));
  char byte;
  EXPECT_EQ(read(sockets[1], &byte, 1), 0);
  close(sockets[1]);
}

TEST_F(RegistryContract, ImmediateCompletionIsRetainedForOneJoin) {
  int sockets[2];
  char byte;
  SessionRegistry successful;
  current.activeRegistry = &successful;
  auto immediate = std::make_unique<SessionState>();
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  immediate->fd = sockets[0];
  immediate->connection_number = 2;
  const auto started = successful.start(std::make_unique<RuntimeSessionWorker>(
      std::move(immediate), finishImmediately));
  if (started != 0)
    close(sockets[1]);
  ASSERT_EQ(started, 0);
  {
    std::unique_lock lock(current.completionMutex);
    current.completionChanged.wait(lock, [&] { return current.finished; });
  }
  successful.joinFinished();
  successful.joinFinished();
  EXPECT_FALSE(successful.cancelAndJoin(2));
  EXPECT_EQ(read(sockets[1], &byte, 1), 0);
  close(sockets[1]);
}

TEST(SessionRegistry, ReplacementPreservesNewPrincipalUntilExplicitClear) {
  RuntimePrincipalSession principal;
  SessionState first{}, replacement{};
  first.connection_number = 3;
  replacement.connection_number = 4;
  ASSERT_TRUE(principal.acquire(first, false).accepted);
  EXPECT_FALSE(principal.acquire(replacement, false).accepted);
  auto changed = principal.acquire(replacement, true);
  EXPECT_TRUE(changed.accepted);
  EXPECT_EQ(changed.previousId, 3);
  EXPECT_FALSE(principal.releaseIfCurrent(3));
  EXPECT_TRUE(principal.isCurrent(4));
  EXPECT_EQ(principal.snapshot().id, 4);
  EXPECT_EQ(principal.clear(), 4);
  EXPECT_FALSE(principal.isCurrent(4));
}

TEST_F(RegistryContract, CallerCancellationDuringRetirementKeepsSessionOwnedUntilWorkerJoin) {
  RuntimePrincipalSession principal;
  SessionRegistry cancellation;
  int sockets[2];
  char byte;
  current.activeRegistry = &cancellation;
  current.activePrincipal = &principal;
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  auto cancellable = std::make_unique<SessionState>();
  cancellable->connection_number = 5;
  cancellable->fd = sockets[0];
  EXPECT_TRUE(principal.acquire(*cancellable, false).accepted);
  const auto started = cancellation.start(std::make_unique<RuntimeSessionWorker>(
      std::move(cancellable), waitForCancellation));
  if (started != 0)
    close(sockets[1]);
  ASSERT_EQ(started, 0);
  pthread_t retiring;
  const auto retiringStarted = pthread_create(&retiring, nullptr, retireSession, nullptr);
  if (retiringStarted != 0) {
    {
      std::lock_guard lock(current.completionMutex);
      current.permitCleanup = true;
    }
    current.completionChanged.notify_all();
    close(sockets[1]);
  }
  ASSERT_EQ(retiringStarted, 0);
  {
    std::unique_lock lock(current.completionMutex);
    current.completionChanged.wait(lock, [&] { return current.cleanupEntered; });
  }
  EXPECT_FALSE(cancellation.cancelAndJoin(5));
  EXPECT_EQ(pthread_cancel(retiring), 0);
  const auto received = recv(sockets[1], &byte, 1, MSG_DONTWAIT);
  const auto receiveError = errno;
  EXPECT_EQ(received, -1);
  EXPECT_EQ(receiveError, EAGAIN);
  {
    std::lock_guard lock(current.completionMutex);
    current.permitCleanup = true;
  }
  current.completionChanged.notify_all();
  void *result = nullptr;
  EXPECT_EQ(pthread_join(retiring, &result), 0);
  EXPECT_EQ(result, PTHREAD_CANCELED);
  EXPECT_TRUE(current.cleanupFinished);
  EXPECT_FALSE(principal.isCurrent(5));
  cancellation.joinFinished();
  EXPECT_FALSE(cancellation.cancelAndJoin(5));
  EXPECT_EQ(read(sockets[1], &byte, 1), 0);
  close(sockets[1]);
}

TEST_F(RegistryContract, BatchCancellationSignalsAllWorkersBeforeJoiningBlockedCleanup) {
  RuntimePrincipalSession principal;
  SessionRegistry cancellation;
  current.activeRegistry = &cancellation;
  current.activePrincipal = &principal;
  int sockets[2];
  char byte;
  pthread_t retiring;
  int secondSockets[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  const auto secondPair = socketpair(AF_UNIX, SOCK_STREAM, 0, secondSockets);
  if (secondPair != 0) {
    close(sockets[0]);
    close(sockets[1]);
  }
  ASSERT_EQ(secondPair, 0);
  auto one = std::make_unique<SessionState>();
  auto two = std::make_unique<SessionState>();
  one->connection_number = 6;
  two->connection_number = 7;
  one->fd = sockets[0];
  two->fd = secondSockets[0];
  const auto firstStarted = cancellation.start(std::make_unique<RuntimeSessionWorker>(
      std::move(one), waitForCancellation));
  const auto secondStarted = cancellation.start(std::make_unique<RuntimeSessionWorker>(
      std::move(two), waitForCancellation));
  const auto retiringStarted = firstStarted == 0 && secondStarted == 0
      ? pthread_create(&retiring, nullptr, retireAll, nullptr) : EAGAIN;
  if (retiringStarted != 0) {
    {
      std::lock_guard lock(current.completionMutex);
      current.permitCleanup = true;
    }
    current.completionChanged.notify_all();
    close(sockets[1]);
    close(secondSockets[1]);
  }
  EXPECT_EQ(firstStarted, 0);
  EXPECT_EQ(secondStarted, 0);
  ASSERT_EQ(retiringStarted, 0);
  {
    std::unique_lock lock(current.completionMutex);
    current.completionChanged.wait(lock, [&] { return current.cleanupCount == 2; });
    current.permitCleanup = true;
  }
  current.completionChanged.notify_all();
  EXPECT_EQ(pthread_join(retiring, nullptr), 0);
  EXPECT_EQ(read(sockets[1], &byte, 1), 0);
  EXPECT_EQ(read(secondSockets[1], &byte, 1), 0);
  close(sockets[1]);
  close(secondSockets[1]);
}
