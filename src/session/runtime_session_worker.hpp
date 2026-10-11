#pragma once

#include "session/managed_session.hpp"
#include "session/session_state.hpp"
#include <memory>

class RuntimeSessionWorker final : public ManagedSession {
public:
  using ThreadCreator = int (*)(pthread_t *, void *(*)(void *), void *);
  RuntimeSessionWorker(std::unique_ptr<SessionState> session, void *(*routine)(void *),
                       ThreadCreator creator = createThread);
  int id() const override;
  airplay_stream_c liveCategory() const override;
  int start() override;
  void requestStop() override;
  void join() override;

private:
  static int createThread(pthread_t *, void *(*)(void *), void *);
  std::unique_ptr<SessionState> session_;
  void *(*routine_)(void *);
  ThreadCreator creator_;
};
