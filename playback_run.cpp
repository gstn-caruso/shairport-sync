#include "playback_run.hpp"
#include <exception>

int PlaybackRun::createThread(pthread_t *thread, Routine routine, void *argument) {
  return pthread_create(thread, nullptr, routine, argument);
}
void PlaybackRun::unlock(void *mutex) {
  pthread_mutex_unlock(static_cast<pthread_mutex_t *>(mutex));
}
void PlaybackRun::markFinished(void *argument) {
  auto *self = static_cast<PlaybackRun *>(argument);
  pthread_mutex_lock(&self->mutex_);
  self->finished_ = true;
  pthread_cond_broadcast(&self->changed_);
  pthread_mutex_unlock(&self->mutex_);
}
void *PlaybackRun::run(void *argument) {
  auto *self = static_cast<PlaybackRun *>(argument);
  void *result;
  pthread_cleanup_push(markFinished, self);
  result = self->routine_(self->argument_);
  pthread_cleanup_pop(1);
  return result;
}
PlaybackRun::StartResult PlaybackRun::start(Routine routine, void *argument, Creator creator) {
  StartResult result = StartResult::alreadyOwned;
  int previousState = PTHREAD_CANCEL_ENABLE;
  bool protectedCreation = false;
  pthread_mutex_lock(&mutex_);
  pthread_cleanup_push(unlock, &mutex_);
  while (state_ == State::stopping) pthread_cond_wait(&changed_, &mutex_);
  if (state_ == State::idle) {
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
    protectedCreation = true;
    routine_ = routine;
    argument_ = argument;
    finished_ = false;
    if (creator(&thread_, run, this) == 0) {
      state_ = State::running;
      result = StartResult::started;
    } else {
      result = StartResult::failed;
    }
  }
  pthread_cleanup_pop(1);
  if (protectedCreation) pthread_setcancelstate(previousState, nullptr);
  return result;
}
bool PlaybackRun::stop() {
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  bool effective = false;
  pthread_mutex_lock(&mutex_);
  if (state_ == State::stopping) {
    const auto completed = completedStops_;
    while (completedStops_ == completed) pthread_cond_wait(&changed_, &mutex_);
    pthread_mutex_unlock(&mutex_);
  } else if (state_ == State::running) {
    state_ = State::stopping;
    const auto thread = thread_;
    pthread_mutex_unlock(&mutex_);
    pthread_cancel(thread);
    if (pthread_join(thread, nullptr) != 0) std::terminate();
    pthread_mutex_lock(&mutex_);
    state_ = State::idle;
    ++completedStops_;
    pthread_cond_broadcast(&changed_);
    pthread_mutex_unlock(&mutex_);
    effective = true;
  } else {
    pthread_mutex_unlock(&mutex_);
  }
  pthread_setcancelstate(previousState, nullptr);
  return effective;
}
bool PlaybackRun::isActive() const {
  pthread_mutex_lock(&mutex_);
  const bool active = state_ == State::running && !finished_;
  pthread_mutex_unlock(&mutex_);
  return active;
}
PlaybackRun::~PlaybackRun() noexcept {
  stop();
  pthread_cond_destroy(&changed_);
  pthread_mutex_destroy(&mutex_);
}
