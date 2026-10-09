#include "session_registry.hpp"
#include <algorithm>
#include <cerrno>
#include <exception>
#include <unistd.h>

SessionState::~SessionState() {
  playbackRun.stop();
  if (fd >= 0)
    close(fd);
}

SessionRegistry::~SessionRegistry() noexcept {
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  joinSessions(takeAllForShutdown(), true);
  // Runtime threads use deferred cancellation. Destruction leaves its pending delivery for the
  // caller's next cancellation point rather than introducing one inside a noexcept destructor.
  pthread_setcancelstate(previousState, nullptr);
}

int SessionRegistry::createThread(pthread_t *thread, void *(*routine)(void *), void *argument) {
  auto *session = static_cast<SessionState *>(argument);
  return named_pthread_create(thread, nullptr, routine, argument, "rtsp_conn_%d",
                              session->connection_number);
}

int SessionRegistry::start(std::unique_ptr<SessionState> session, void *(*routine)(void *)) {
  std::unique_ptr<SessionState> failed;
  int result;
  {
    std::lock_guard lock(mutex_);
    if (closed_)
      return ECANCELED;
    finished_.reserve(sessions_.size() + 1);
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
  std::erase(finished_, id);
  return session;
}

void SessionRegistry::markFinished(int id) {
  std::lock_guard lock(mutex_);
  if (std::ranges::any_of(sessions_, [id](const auto &session) {
        return session->connection_number == id;
      }) && std::ranges::find(finished_, id) == finished_.end())
    finished_.push_back(id);
}

std::vector<std::unique_ptr<SessionState>> SessionRegistry::takeFinished() {
  std::vector<std::unique_ptr<SessionState>> completed;
  std::lock_guard lock(mutex_);
  completed.reserve(finished_.size());
  for (auto position = sessions_.begin(); position != sessions_.end();) {
    if (std::ranges::find(finished_, (*position)->connection_number) == finished_.end()) {
      ++position;
      continue;
    }
    completed.push_back(std::move(*position));
    position = sessions_.erase(position);
  }
  finished_.clear();
  return completed;
}

std::vector<std::unique_ptr<SessionState>>
SessionRegistry::takeMatching(airplay_stream_c category, int exceptId) {
  std::vector<std::unique_ptr<SessionState>> matching;
  std::lock_guard lock(mutex_);
  matching.reserve(sessions_.size());
  for (auto position = sessions_.begin(); position != sessions_.end();) {
    auto &session = *position;
    if (session->connection_number == exceptId ||
        (category != unspecified_stream_category && session->airplay_stream_category != category)) {
      ++position;
      continue;
    }
    std::erase(finished_, session->connection_number);
    matching.push_back(std::move(session));
    position = sessions_.erase(position);
  }
  return matching;
}

void SessionRegistry::joinSessions(std::vector<std::unique_ptr<SessionState>> sessions, bool cancel) {
  if (cancel)
    for (const auto &session : sessions)
      pthread_cancel(session->thread);
  for (const auto &session : sessions)
    if (pthread_join(session->thread, nullptr) != 0)
      std::terminate();
}

bool SessionRegistry::cancelAndJoin(int id) {
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  bool found;
  {
    auto session = takeById(id);
    found = session != nullptr;
    if (session) {
      pthread_cancel(session->thread);
      if (pthread_join(session->thread, nullptr) != 0)
        std::terminate();
    }
  }
  pthread_setcancelstate(previousState, nullptr);
  pthread_testcancel();
  return found;
}

void SessionRegistry::cancelAndJoinMatching(airplay_stream_c category, int exceptId) {
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  joinSessions(takeMatching(category, exceptId), true);
  pthread_setcancelstate(previousState, nullptr);
  pthread_testcancel();
}

void SessionRegistry::joinFinished() {
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  joinSessions(takeFinished(), false);
  pthread_setcancelstate(previousState, nullptr);
  pthread_testcancel();
}

std::vector<std::unique_ptr<SessionState>> SessionRegistry::takeAllForShutdown() {
  std::lock_guard lock(mutex_);
  closed_ = true;
  finished_.clear();
  return std::move(sessions_);
}

void SessionRegistry::shutdown() {
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  joinSessions(takeAllForShutdown(), true);
  pthread_setcancelstate(previousState, nullptr);
  pthread_testcancel();
}
