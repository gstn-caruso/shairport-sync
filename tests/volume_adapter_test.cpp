#include "session_state.hpp"
#include "volume_runtime.hpp"
#include "audio_player_adapter.hpp"
#include <cassert>
#include <vector>

static SessionState session{};
static volume_range_t hardwareRange{-4000, 0};
static output_parameters_t parameters{&hardwareRange};
static std::vector<int> effects;
static int muteResult = 0;
static output_parameters_t *readParameters() {
  assert(suggested_volume(&session) == sharedVolumeLevel.current());
  session.volumeControl.pcmSnapshot();
  return &parameters;
}
static void volume(double value) {
  effects.push_back(1);
  assert(value == -1000);
  assert(session.volumeControl.pcmSnapshot().gainFixed16 == 1234);
}
static int mute(int enabled) {
  effects.push_back(enabled ? 3 : 2);
  if (!enabled) assert(session.volumeControl.pcmSnapshot().gainFixed16 == 65536);
  session.volumeControl.suggestedLevel(sharedVolumeLevel);
  return muteResult;
}

int main() {
  audio_output backend{};
  backend.parameters = readParameters;
  backend.volume = volume;
  backend.mute = mute;
  config.output = &backend;
  config.volume_control_profile = VCP_flat;
  config.volume_range_db = 20;
  session.volumeControl.apply({.gainFixed16 = 1234}, false);
  applySessionVolume(-15, session);
  assert((effects == std::vector<int>{1, 2}));
  assert(!session.volumeControl.pcmSnapshot().softwareMuted);
  effects.clear();
  muteResult = 1;
  applySessionVolume(-144, session);
  assert((effects == std::vector<int>{3}));
  assert(session.volumeControl.pcmSnapshot().softwareMuted);
  config.ignore_volume_control = 1;
  effects.clear();
  const auto before = session.volumeControl.pcmSnapshot();
  player_volume_without_notification(-144, &session);
  assert(effects.empty() && sharedVolumeLevel.current() == -144);
  assert(session.volumeControl.pcmSnapshot().gainFixed16 == before.gainFixed16);
  assert(session.volumeControl.pcmSnapshot().softwareMuted == before.softwareMuted);
  sharedVolumeLevel.remember(-24);
  assert(suggested_volume(&session) == -24);
}
