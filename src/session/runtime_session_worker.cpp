#include "session/runtime_session_worker.hpp"
#include <exception>

RuntimeSessionWorker::RuntimeSessionWorker(std::unique_ptr<SessionState> session,
                                         void *(*routine)(void *), ThreadCreator creator)
    : session_(std::move(session)), routine_(routine), creator_(creator) {}

int RuntimeSessionWorker::id() const { return session_->connection_number; }

airplay_stream_c RuntimeSessionWorker::liveCategory() const {
  return session_->airplay_stream_category.load();
}

int RuntimeSessionWorker::createThread(pthread_t *thread, void *(*routine)(void *), void *argument) {
  auto *session = static_cast<SessionState *>(argument);
  return named_pthread_create(thread, nullptr, routine, argument, "rtsp_conn_%d",
                              session->connection_number);
}

int RuntimeSessionWorker::start() {
  return creator_(&session_->thread, routine_, session_.get());
}

void RuntimeSessionWorker::requestStop() { pthread_cancel(session_->thread); }

void RuntimeSessionWorker::join() {
  if (pthread_join(session_->thread, nullptr) != 0)
    std::terminate();
}
