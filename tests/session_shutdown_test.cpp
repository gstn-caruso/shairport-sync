#include "session/session_registry.hpp"
#include "cancellation_wait.hpp"
#include <gtest/gtest.h>
#include <cerrno>
#include <condition_variable>
#include <sys/socket.h>
#include <unistd.h>

struct ShutdownScenario {
  CancellationWait cancellation;
  std::mutex mutex;
  std::condition_variable changed;
  SessionRegistry *registry = nullptr;
  int sockets[2];
  int startError = 0;
  bool ready = false, destroy = false, cleanupEntered = false, allowCleanup = false;
  bool cleanupFinished = false, destructionReturned = false;
};
static ShutdownScenario *scenario;

static void finishWorker(void *argument) {
  auto *session = static_cast<SessionState *>(argument);
  {
    std::unique_lock lock(scenario->mutex);
    scenario->cleanupEntered = true;
    scenario->changed.notify_all();
    scenario->changed.wait(lock, [] { return scenario->allowCleanup; });
  }
  scenario->registry->markFinished(session->connection_number);
  {
    std::lock_guard lock(scenario->mutex);
    scenario->cleanupFinished = true;
  }
  scenario->changed.notify_all();
}
static void *blockWorker(void *argument) {
  pthread_cleanup_push(finishWorker, argument);
  {
    std::lock_guard lock(scenario->mutex);
    scenario->ready = true;
  }
  scenario->changed.notify_all();
  scenario->cancellation.block();
  pthread_cleanup_pop(1);
  return nullptr;
}
static bool startWorker(SessionRegistry &registry) {
  scenario->registry = &registry;
  auto session = std::make_unique<SessionState>();
  session->connection_number = 1;
  session->fd = scenario->sockets[0];
  const auto result = registry.start(std::move(session), blockWorker);
  {
    std::lock_guard lock(scenario->mutex);
    scenario->startError = result;
  }
  scenario->changed.notify_all();
  return result == 0;
}
static void awaitDestruction() {
  std::unique_lock lock(scenario->mutex);
  scenario->changed.wait(lock, [] { return scenario->destroy; });
}
static void announceDestruction() {
  std::lock_guard lock(scenario->mutex);
  EXPECT_TRUE(scenario->cleanupFinished);
  scenario->destructionReturned = true;
}
static void *destroyStack(void *) {
  {
    SessionRegistry registry;
    if (!startWorker(registry))
      return nullptr;
    awaitDestruction();
  }
  announceDestruction();
  pthread_testcancel();
  return nullptr;
}
static void *destroyUnique(void *) {
  auto registry = std::make_unique<SessionRegistry>();
  if (!startWorker(*registry))
    return nullptr;
  awaitDestruction();
  registry.reset();
  announceDestruction();
  pthread_testcancel();
  return nullptr;
}
struct UnwindRequested {};
static void *unwindStack(void *) {
  try {
    SessionRegistry registry;
    if (!startWorker(registry))
      return nullptr;
    awaitDestruction();
    throw UnwindRequested{};
  } catch (const UnwindRequested &) {
    announceDestruction();
  }
  pthread_testcancel();
  return nullptr;
}
static void *unwindUnique(void *) {
  try {
    auto registry = std::make_unique<SessionRegistry>();
    if (!startWorker(*registry))
      return nullptr;
    awaitDestruction();
    throw UnwindRequested{};
  } catch (const UnwindRequested &) {
    announceDestruction();
  }
  pthread_testcancel();
  return nullptr;
}
static void announceAfterUnwind(void *) { announceDestruction(); }
static void *cancelStack(void *) {
  pthread_cleanup_push(announceAfterUnwind, nullptr);
  {
    SessionRegistry registry;
    if (!startWorker(registry))
      return nullptr;
    awaitDestruction();
  }
  pthread_cleanup_pop(0);
  return nullptr;
}
static void *cancelUnique(void *) {
  pthread_cleanup_push(announceAfterUnwind, nullptr);
  {
    auto registry = std::make_unique<SessionRegistry>();
    if (!startWorker(*registry))
      return nullptr;
    awaitDestruction();
  }
  pthread_cleanup_pop(0);
  return nullptr;
}
static void *shutdownTwice(void *) {
  SessionRegistry registry;
  if (!startWorker(registry))
    return nullptr;
  awaitDestruction();
  registry.shutdown();
  registry.shutdown();
  announceDestruction();
  int sockets[2];
  const auto paired = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
  EXPECT_EQ(paired, 0);
  if (paired != 0)
    return nullptr;
  auto rejected = std::make_unique<SessionState>();
  rejected->fd = sockets[0];
  EXPECT_EQ(registry.start(std::move(rejected), blockWorker), ECANCELED);
  char byte;
  EXPECT_EQ(read(sockets[1], &byte, 1), 0);
  close(sockets[1]);
  return nullptr;
}
static void checkDestructor(void *(*destroyer)(void *), bool cancelDestroyer,
                            bool cancelWhileWaiting = false) {
  ShutdownScenario current;
  struct RestoreScenario {
    ShutdownScenario *saved = scenario;
    ~RestoreScenario() { scenario = saved; }
  } restore;
  scenario = &current;
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, current.sockets), 0);
  pthread_t retiring;
  const auto created = pthread_create(&retiring, nullptr, destroyer, nullptr);
  if (created != 0) {
    close(current.sockets[0]);
    close(current.sockets[1]);
  }
  ASSERT_EQ(created, 0);
  int startError;
  {
    std::unique_lock lock(current.mutex);
    current.changed.wait(lock, [&] { return current.ready || current.startError != 0; });
    current.destroy = !cancelWhileWaiting;
    startError = current.startError;
  }
  if (startError != 0) {
    EXPECT_EQ(pthread_join(retiring, nullptr), 0);
    close(current.sockets[1]);
    FAIL() << "Session worker creation failed: " << startError;
  }
  current.changed.notify_all();
  if (cancelWhileWaiting)
    EXPECT_EQ(pthread_cancel(retiring), 0);
  {
    std::unique_lock lock(current.mutex);
    current.changed.wait(lock, [&] { return current.cleanupEntered; });
  }
  if (cancelDestroyer && !cancelWhileWaiting)
    EXPECT_EQ(pthread_cancel(retiring), 0);
  char byte;
  const auto received = recv(current.sockets[1], &byte, 1, MSG_DONTWAIT);
  const auto receiveError = errno;
  EXPECT_EQ(received, -1);
  EXPECT_EQ(receiveError, EAGAIN);
  {
    std::lock_guard lock(current.mutex);
    current.allowCleanup = true;
  }
  current.changed.notify_all();
  void *result = nullptr;
  EXPECT_EQ(pthread_join(retiring, &result), 0);
  EXPECT_EQ(result, (cancelDestroyer ? PTHREAD_CANCELED : nullptr));
  EXPECT_TRUE(current.destructionReturned);
  EXPECT_TRUE(current.cleanupFinished);
  EXPECT_EQ(read(current.sockets[1], &byte, 1), 0);
  close(current.sockets[1]);
}

TEST(SessionShutdown, StackDestructionCancelsAndJoinsBeforeClosingSocket) {
  checkDestructor(destroyStack, false);
}

TEST(SessionShutdown, UniqueOwnershipDestructionCancelsAndJoinsBeforeClosingSocket) {
  checkDestructor(destroyUnique, false);
}

TEST(SessionShutdown, PendingCancellationIsDeliveredAfterStackDestructionReturns) {
  checkDestructor(destroyStack, true);
}

TEST(SessionShutdown, PendingCancellationIsDeliveredAfterUniqueOwnershipDestructionReturns) {
  checkDestructor(destroyUnique, true);
}

TEST(SessionShutdown, PendingCancellationDuringExceptionUnwindCompletesStackCleanup) {
  checkDestructor(unwindStack, true);
}

TEST(SessionShutdown, PendingCancellationDuringExceptionUnwindCompletesUniqueOwnershipCleanup) {
  checkDestructor(unwindUnique, true);
}

TEST(SessionShutdown, PthreadCancellationUnwindsStackOwnerAndJoinsWorker) {
  checkDestructor(cancelStack, true, true);
}

TEST(SessionShutdown, PthreadCancellationUnwindsUniqueOwnerAndJoinsWorker) {
  checkDestructor(cancelUnique, true, true);
}

TEST(SessionShutdown, ExplicitShutdownIsIdempotentAndRejectsNewSessions) {
  checkDestructor(shutdownTwice, false);
}
