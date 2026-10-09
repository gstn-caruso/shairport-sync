#include "session_registry.hpp"
#include <algorithm>
#include <unistd.h>

SessionState::~SessionState() {
  if (fd >= 0)
    close(fd);
}

int SessionRegistry::createThread(pthread_t *thread, void *(*routine)(void *), void *argument) {
  return named_pthread_create(thread, nullptr, routine, argument, "rtsp_conversation");
}

int SessionRegistry::start(std::unique_ptr<SessionState> session, void *(*routine)(void *)) {
  std::unique_ptr<SessionState> failed;
  int result;
  {
    std::lock_guard lock(mutex_);
    sessions_.push_back(std::move(session));
    auto &starting = sessions_.back();
    result = creator_(&starting->thread, routine, starting.get());
    if (result != 0) {
      failed = std::move(starting);
      sessions_.pop_back();
    }
  }
  return result;
}

std::unique_ptr<SessionState> SessionRegistry::takeById(int id) {
  std::lock_guard lock(mutex_);
  auto position = std::find_if(sessions_.begin(), sessions_.end(),
                               [id](const auto &session) { return session->connection_number == id; });
  if (position == sessions_.end())
    return {};
  auto session = std::move(*position);
  sessions_.erase(position);
  return session;
}
