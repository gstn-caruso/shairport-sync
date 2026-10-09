#include "session_state.hpp"
#include "volume_runtime.hpp"
#include <cassert>
#include <condition_variable>
#include <thread>

static SessionState session{};
static std::mutex ordering;
static std::condition_variable changed;
static bool startupPaused = false, releaseStartup = false, setterArrived = false;
static bool setterContended = false, setterFinished = false, hardwareMuted = false;
static thread_local bool setter = false, observedLock = false;
extern "C" int __real_pthread_mutex_lock(pthread_mutex_t *);
extern "C" int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex) {
  if (setter && !observedLock) {
    observedLock = true;
    const int probe = pthread_mutex_trylock(mutex);
    if (probe == 0) pthread_mutex_unlock(mutex);
    {
      std::lock_guard lock(ordering);
      setterContended = probe != 0;
      setterArrived = true;
    }
    changed.notify_all();
  }
  return __real_pthread_mutex_lock(mutex);
}
static output_parameters_t *parameters() {
  session.volumeControl.pcmSnapshot();
  static volume_range_t range{-4000, 0};
  static output_parameters_t output{&range};
  return &output;
}
static void volume(double) {
  session.volumeControl.pcmSnapshot();
  std::unique_lock lock(ordering);
  startupPaused = true;
  changed.notify_all();
  changed.wait(lock, [] { return releaseStartup; });
}
static int mute(int enabled) {
  session.volumeControl.pcmSnapshot();
  hardwareMuted = enabled;
  return 0;
}
int main() {
  audio_output backend{};
  backend.parameters = parameters;
  backend.volume = volume;
  backend.mute = mute;
  config.output = &backend;
  config.volume_control_profile = VCP_flat;
  config.volume_range_db = 20;
  sharedVolumeLevel.remember(0);
  std::thread startup([] { player_volume(-15, &session); });
  {
    std::unique_lock lock(ordering);
    changed.wait(lock, [] { return startupPaused; });
  }
  session.volumeControl.rememberLevel(-144);
  std::thread setParameter([] {
    setter = true;
    player_volume(-144, &session);
    { std::lock_guard lock(ordering); setterFinished = true; }
    changed.notify_all();
  });
  {
    std::unique_lock lock(ordering);
    changed.wait(lock, [] { return setterArrived; });
    if (!setterContended) changed.wait(lock, [] { return setterFinished; });
    releaseStartup = true;
  }
  changed.notify_all();
  startup.join();
  setParameter.join();
  assert(hardwareMuted);
  assert(suggested_volume(&session) == -144 && sharedVolumeLevel.current() == -144);
}
