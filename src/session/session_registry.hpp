#pragma once

#include "session/managed_session.hpp"
#include <memory>
#include <mutex>
#include <vector>

class SessionRegistry {
public:
  ~SessionRegistry() noexcept;
  int start(std::unique_ptr<ManagedSession> session);
  void markFinished(int id);
  bool cancelAndJoin(int id);
  void cancelAndJoinMatching(airplay_stream_c category, int exceptId);
  void joinFinished();
  void shutdown();

private:
  std::unique_ptr<ManagedSession> takeById(int id);
  std::vector<std::unique_ptr<ManagedSession>> takeFinished();
  std::vector<std::unique_ptr<ManagedSession>> takeMatching(airplay_stream_c category, int exceptId);
  std::vector<std::unique_ptr<ManagedSession>> takeAllForShutdown();
  static void joinSessions(std::vector<std::unique_ptr<ManagedSession>> sessions, bool cancel);
  std::mutex mutex_;
  std::vector<std::unique_ptr<ManagedSession>> sessions_;
  std::vector<int> finished_;
  bool closed_ = false;
};
