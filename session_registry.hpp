#pragma once

#include "session_state.hpp"
#include <memory>
#include <mutex>
#include <vector>

class SessionRegistry {
public:
  using ThreadCreator = int (*)(pthread_t *, void *(*)(void *), void *);
  explicit SessionRegistry(ThreadCreator creator = createThread) : creator_(creator) {}
  int start(std::unique_ptr<SessionState> session, void *(*routine)(void *));
  std::unique_ptr<SessionState> takeById(int id);
  void markFinished(int id);
  std::vector<std::unique_ptr<SessionState>> takeFinished();

private:
  static int createThread(pthread_t *, void *(*)(void *), void *);
  ThreadCreator creator_;
  std::mutex mutex_;
  std::vector<std::unique_ptr<SessionState>> sessions_;
  std::vector<int> finished_;
};
