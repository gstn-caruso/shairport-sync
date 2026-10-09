#pragma once
#include <pthread.h>

// LLVM 23.1.3 restores interceptor state when cancellation exits pthread_cond_wait.
// Lifecycle fixtures use that cancellation point so cleanup mutexes remain observable to TSan.
class CancellationWait {
public:
  ~CancellationWait() {
    pthread_cond_destroy(&changed_);
    pthread_mutex_destroy(&mutex_);
  }
  void block() {
    pthread_mutex_lock(&mutex_);
    pthread_cleanup_push(unlock, &mutex_);
    ++blocked_;
    pthread_cond_broadcast(&changed_);
    for (;;) pthread_cond_wait(&changed_, &mutex_);
    pthread_cleanup_pop(1);
  }
  void waitForBlocked(unsigned count) {
    pthread_mutex_lock(&mutex_);
    pthread_cleanup_push(unlock, &mutex_);
    while (blocked_ < count) pthread_cond_wait(&changed_, &mutex_);
    pthread_cleanup_pop(1);
  }
private:
  static void unlock(void *mutex) {
    pthread_mutex_unlock(static_cast<pthread_mutex_t *>(mutex));
  }
  pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t changed_ = PTHREAD_COND_INITIALIZER;
  unsigned blocked_ = 0;
};
