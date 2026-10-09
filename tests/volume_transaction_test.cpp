#include "session_state.hpp"
#include "volume_runtime.hpp"
#include <gtest/gtest.h>
#include <condition_variable>
#include <thread>

struct VolumeTransactionState {
  SessionState session{};
  std::mutex ordering;
  std::condition_variable changed;
  bool startupPaused = false, releaseStartup = false, setterArrived = false;
  bool setterContended = false, setterFinished = false, hardwareMuted = false;
  volume_range_t range{-4000, 0};
  output_parameters_t output{&range};
};

static VolumeTransactionState *activeState;
static thread_local bool setter = false, observedLock = false;
extern "C" int __real_pthread_mutex_lock(pthread_mutex_t *);
extern "C" int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex) {
  if (setter && !observedLock) {
    observedLock = true;
    const int probe = pthread_mutex_trylock(mutex);
    if (probe == 0) pthread_mutex_unlock(mutex);
    {
      std::lock_guard lock(activeState->ordering);
      activeState->setterContended = probe != 0;
      activeState->setterArrived = true;
    }
    activeState->changed.notify_all();
  }
  return __real_pthread_mutex_lock(mutex);
}
static output_parameters_t *parameters() {
  activeState->session.volumeControl.pcmSnapshot();
  return &activeState->output;
}
static void volume(double) {
  activeState->session.volumeControl.pcmSnapshot();
  std::unique_lock lock(activeState->ordering);
  activeState->startupPaused = true;
  activeState->changed.notify_all();
  activeState->changed.wait(lock, [] { return activeState->releaseStartup; });
}
static int mute(int enabled) {
  activeState->session.volumeControl.pcmSnapshot();
  activeState->hardwareMuted = enabled;
  return 0;
}
TEST(VolumeTransaction, ConcurrentSetterMuteCannotBeOverwrittenByStartup) {
  const auto savedState = activeState;
  const auto savedOutput = config.output;
  const auto savedProfile = config.volume_control_profile;
  const auto savedRange = config.volume_range_db;
  const auto savedSharedLevel = sharedVolumeLevel.current();
  VolumeTransactionState state;
  activeState = &state;
  audio_output backend{};
  backend.parameters = parameters;
  backend.volume = volume;
  backend.mute = mute;
  config.output = &backend;
  config.volume_control_profile = VCP_flat;
  config.volume_range_db = 20;
  sharedVolumeLevel.remember(0);
  std::thread startup([&] { player_volume(-15, &state.session); });
  {
    std::unique_lock lock(state.ordering);
    state.changed.wait(lock, [&] { return state.startupPaused; });
  }
  state.session.volumeControl.rememberLevel(-144);
  std::thread setParameter([&] {
    setter = true;
    player_volume(-144, &state.session);
    { std::lock_guard lock(state.ordering); state.setterFinished = true; }
    state.changed.notify_all();
  });
  {
    std::unique_lock lock(state.ordering);
    state.changed.wait(lock, [&] { return state.setterArrived; });
    if (!state.setterContended) state.changed.wait(lock, [&] { return state.setterFinished; });
    state.releaseStartup = true;
  }
  state.changed.notify_all();
  startup.join();
  setParameter.join();
  EXPECT_TRUE(state.hardwareMuted);
  EXPECT_EQ(suggested_volume(&state.session), -144);
  EXPECT_EQ(sharedVolumeLevel.current(), -144);
  config.output = savedOutput;
  config.volume_control_profile = savedProfile;
  config.volume_range_db = savedRange;
  sharedVolumeLevel.remember(savedSharedLevel);
  activeState = savedState;
}
