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
  void markFinished(int id);
  bool cancelAndJoin(int id);
  void cancelAndJoinMatching(airplay_stream_c category, int exceptId);
  void joinFinished();

private:
  std::unique_ptr<SessionState> takeById(int id);
  std::vector<std::unique_ptr<SessionState>> takeFinished();
  std::vector<std::unique_ptr<SessionState>> takeMatching(airplay_stream_c category, int exceptId);
  static int createThread(pthread_t *, void *(*)(void *), void *);
  static void joinSessions(std::vector<std::unique_ptr<SessionState>> sessions, bool cancel);
  ThreadCreator creator_;
  std::mutex mutex_;
  std::vector<std::unique_ptr<SessionState>> sessions_;
  std::vector<int> finished_;
};
