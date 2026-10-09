#include "session/session_state.hpp"
#include "volume/volume_runtime.hpp"
#include "audio/output/audio_player_adapter.hpp"
#include <gtest/gtest.h>
#include <cassert>
#include <vector>

struct VolumeAdapterState {
  SessionState session{};
  volume_range_t hardwareRange{-4000, 0};
  output_parameters_t parameters{&hardwareRange};
  std::vector<int> effects;
  int muteResult = 0;
};

static VolumeAdapterState *activeState;
static output_parameters_t *readParameters() {
  assert(suggested_volume(&activeState->session) == sharedVolumeLevel.current());
  activeState->session.volumeControl.pcmSnapshot();
  return &activeState->parameters;
}
static void volume(double value) {
  activeState->effects.push_back(1);
  assert(value == -1000);
  assert(activeState->session.volumeControl.pcmSnapshot().gainFixed16 == 1234);
}
static int mute(int enabled) {
  activeState->effects.push_back(enabled ? 3 : 2);
  if (!enabled) assert(activeState->session.volumeControl.pcmSnapshot().gainFixed16 == 65536);
  activeState->session.volumeControl.suggestedLevel(sharedVolumeLevel);
  return activeState->muteResult;
}

static void withBackend(void (*scenario)(VolumeAdapterState &)) {
  const auto savedState = activeState;
  const auto savedOutput = config.output;
  const auto savedProfile = config.volume_control_profile;
  const auto savedRange = config.volume_range_db;
  const auto savedIgnore = config.ignore_volume_control;
  const auto savedSharedLevel = sharedVolumeLevel.current();
  VolumeAdapterState state;
  activeState = &state;
  audio_output backend{};
  backend.parameters = readParameters;
  backend.volume = volume;
  backend.mute = mute;
  config.output = &backend;
  config.volume_control_profile = VCP_flat;
  config.volume_range_db = 20;
  config.ignore_volume_control = 0;
  scenario(state);
  config.output = savedOutput;
  config.volume_control_profile = savedProfile;
  config.volume_range_db = savedRange;
  config.ignore_volume_control = savedIgnore;
  sharedVolumeLevel.remember(savedSharedLevel);
  activeState = savedState;
}

static void checkGainAndUnmuteOrdering(VolumeAdapterState &state) {
  auto &session = state.session;
  session.volumeControl.apply({.gainFixed16 = 1234}, false);
  applySessionVolume(-15, session);
  assert((state.effects == std::vector<int>{1, 2}));
  assert(!session.volumeControl.pcmSnapshot().softwareMuted);
}

static void checkHardwareMuteFailure(VolumeAdapterState &state) {
  checkGainAndUnmuteOrdering(state);
  auto &session = state.session;
  state.effects.clear();
  state.muteResult = 1;
  applySessionVolume(-144, session);
  assert((state.effects == std::vector<int>{3}));
  assert(session.volumeControl.pcmSnapshot().softwareMuted);
}

static void checkIgnoredVolumeAfterMute(VolumeAdapterState &state) {
  checkHardwareMuteFailure(state);
  auto &session = state.session;
  config.ignore_volume_control = 1;
  state.effects.clear();
  const auto before = session.volumeControl.pcmSnapshot();
  player_volume_without_notification(-144, &session);
  assert(state.effects.empty() && sharedVolumeLevel.current() == -144);
  assert(session.volumeControl.pcmSnapshot().gainFixed16 == before.gainFixed16);
  assert(session.volumeControl.pcmSnapshot().softwareMuted == before.softwareMuted);
  sharedVolumeLevel.remember(-24);
  assert(suggested_volume(&session) == -24);
}

TEST(VolumeAdapter, HardwareGainPrecedesSoftwareUpdateAndUnmute) {
  withBackend(checkGainAndUnmuteOrdering);
}

TEST(VolumeAdapter, FailedHardwareMuteFallsBackToSoftwareMuteAfterGainUpdate) {
  withBackend(checkHardwareMuteFailure);
}

TEST(VolumeAdapter, IgnoredVolumeRemembersSharedLevelWithoutChangingMutedPcm) {
  withBackend(checkIgnoredVolumeAfterMute);
}
