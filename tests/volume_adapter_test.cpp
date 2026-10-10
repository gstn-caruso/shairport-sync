#include "session/session_state.hpp"
#include "volume/volume_runtime.hpp"
#include "audio/output/audio_player_adapter.hpp"
#include <gtest/gtest.h>
#include <vector>

struct VolumeAdapterState {
  enum class Effect { hardwareGain, hardwareUnmute, hardwareMute };
  SessionState session{};
  volume_range_t hardwareRange{-4000, 0};
  output_parameters_t parameters{&hardwareRange};
  std::vector<Effect> effects;
  int muteResult = 0;
};

void PrintTo(VolumeAdapterState::Effect effect, std::ostream *output) {
  switch (effect) {
  case VolumeAdapterState::Effect::hardwareGain: *output << "hardware gain"; break;
  case VolumeAdapterState::Effect::hardwareUnmute: *output << "hardware unmute"; break;
  case VolumeAdapterState::Effect::hardwareMute: *output << "hardware mute"; break;
  }
}

static VolumeAdapterState *activeState;
static output_parameters_t *readParameters() {
  EXPECT_DOUBLE_EQ(suggested_volume(&activeState->session), sharedVolumeLevel.current().value());
  activeState->session.volumeControl.pcmSnapshot();
  return &activeState->parameters;
}
static void volume(double value) {
  activeState->effects.push_back(VolumeAdapterState::Effect::hardwareGain);
  EXPECT_DOUBLE_EQ(value, -1000);
  EXPECT_EQ(activeState->session.volumeControl.pcmSnapshot().gainFixed16, FixedGain16{1234});
}
static int mute(int enabled) {
  activeState->effects.push_back(enabled ? VolumeAdapterState::Effect::hardwareMute : VolumeAdapterState::Effect::hardwareUnmute);
  if (!enabled)
    EXPECT_EQ(activeState->session.volumeControl.pcmSnapshot().gainFixed16, FixedGain16{65536});
  activeState->session.volumeControl.suggestedLevel(sharedVolumeLevel);
  return activeState->muteResult;
}

class VolumeAdapter : public testing::Test {
protected:
  using Effect = VolumeAdapterState::Effect;
  VolumeAdapterState *savedState = activeState;
  decltype(config.output) savedOutput = config.output;
  decltype(config.volume_control_profile) savedProfile = config.volume_control_profile;
  decltype(config.volume_range_db) savedRange = config.volume_range_db;
  decltype(config.ignore_volume_control) savedIgnore = config.ignore_volume_control;
  AirPlayVolume savedSharedLevel = sharedVolumeLevel.current();
  VolumeAdapterState state;
  audio_output backend{};

  VolumeAdapter() {
    activeState = &state;
    backend.parameters = readParameters;
    backend.volume = volume;
    backend.mute = mute;
    config.output = &backend;
    config.volume_control_profile = VCP_flat;
    config.volume_range_db = 20;
    config.ignore_volume_control = 0;
  }
  ~VolumeAdapter() override {
    config.output = savedOutput;
    config.volume_control_profile = savedProfile;
    config.volume_range_db = savedRange;
    config.ignore_volume_control = savedIgnore;
    sharedVolumeLevel.remember(savedSharedLevel);
    activeState = savedState;
  }
};

TEST_F(VolumeAdapter, HardwareGainPrecedesSoftwareUpdateAndUnmute) {
  auto &session = state.session;
  session.volumeControl.apply({.gainFixed16 = FixedGain16{1234}}, false);
  applySessionVolume(-15, session);
  EXPECT_EQ(state.effects, (std::vector{Effect::hardwareGain, Effect::hardwareUnmute}));
  EXPECT_FALSE(session.volumeControl.pcmSnapshot().softwareMuted);
}

TEST_F(VolumeAdapter, FailedHardwareMuteFallsBackToSoftwareMute) {
  auto &session = state.session;
  state.muteResult = 1;
  applySessionVolume(-144, session);
  EXPECT_EQ(state.effects, std::vector{Effect::hardwareMute});
  EXPECT_TRUE(session.volumeControl.pcmSnapshot().softwareMuted);
}

TEST_F(VolumeAdapter, IgnoredVolumeRemembersSharedLevelWithoutChangingMutedPcm) {
  auto &session = state.session;
  session.volumeControl.apply({.requestMute = true}, false);
  config.ignore_volume_control = 1;
  const auto before = session.volumeControl.pcmSnapshot();
  player_volume_without_notification(-144, &session);
  EXPECT_TRUE(state.effects.empty());
  EXPECT_EQ(sharedVolumeLevel.current(), AirPlayVolume{-144});
  EXPECT_EQ(session.volumeControl.pcmSnapshot().gainFixed16, before.gainFixed16);
  EXPECT_EQ(session.volumeControl.pcmSnapshot().softwareMuted, before.softwareMuted);
  sharedVolumeLevel.remember(AirPlayVolume{-24});
  EXPECT_DOUBLE_EQ(suggested_volume(&session), -24);
}

TEST_F(VolumeAdapter, IgnoringVolumeAfterHardwareMuteFailureRetainsAppliedGain) {
  auto &session = state.session;
  session.volumeControl.apply({.gainFixed16 = FixedGain16{1234}}, false);
  applySessionVolume(-15, session);
  state.muteResult = 1;
  applySessionVolume(-144, session);
  config.ignore_volume_control = 1;

  player_volume_without_notification(-144, &session);

  EXPECT_EQ(state.effects, (std::vector{Effect::hardwareGain, Effect::hardwareUnmute,
                                       Effect::hardwareMute}));
  EXPECT_EQ(sharedVolumeLevel.current(), AirPlayVolume{-144});
  EXPECT_EQ(session.volumeControl.pcmSnapshot().gainFixed16, FixedGain16{65536});
  EXPECT_TRUE(session.volumeControl.pcmSnapshot().softwareMuted);
}
