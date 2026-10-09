#pragma once
#include <pthread.h>
#include <functional>
#include <cstdint>

class PlaybackRun {
public:
  using Routine = void *(*)(void *);
  using Creator = std::function<int(pthread_t *, Routine, void *)>;
  enum class StartResult { started, alreadyOwned, failed };
  PlaybackRun() = default;
  ~PlaybackRun() noexcept;
  PlaybackRun(const PlaybackRun &) = delete;
  PlaybackRun &operator=(const PlaybackRun &) = delete;
  StartResult start(Routine routine, void *argument, Creator creator = createThread);
  bool stop();
  bool isActive() const;
private:
  enum class State { idle, running, stopping };
  static int createThread(pthread_t *, Routine, void *);
  static void *run(void *);
  static void markFinished(void *);
  static void unlock(void *);
  mutable pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t changed_ = PTHREAD_COND_INITIALIZER;
  pthread_t thread_{};
  Routine routine_ = nullptr;
  void *argument_ = nullptr;
  State state_ = State::idle;
  bool finished_ = false;
  uint64_t completedStops_ = 0;
};
