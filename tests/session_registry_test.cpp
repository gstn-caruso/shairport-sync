#include "session_registry.hpp"
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
}
