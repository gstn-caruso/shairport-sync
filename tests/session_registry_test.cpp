#include "session_registry.hpp"
#include <cassert>
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

static int rejectThread(pthread_t *, void *(*)(void *), void *) { return EAGAIN; }
static void *unusedThread(void *) { assert(false); return nullptr; }

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
}
