#include "volume/volume_control.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <type_traits>

static_assert(std::is_same_v<decltype(SharedVolumeLevel{}.current()), AirPlayVolume>);
static_assert(std::is_same_v<decltype(PcmVolumeSnapshot{}.gainFixed16), FixedGain16>);

TEST(VolumeControl, UnrememberedControlsFollowSharedLevel) {
  SharedVolumeLevel shared(AirPlayVolume{-24});
  VolumeControl first, second;
  EXPECT_EQ(first.suggestedLevel(shared), AirPlayVolume{-24});
  shared.remember(AirPlayVolume{-15});
  EXPECT_EQ(first.suggestedLevel(shared), AirPlayVolume{-15});
  EXPECT_EQ(second.suggestedLevel(shared), AirPlayVolume{-15});
}

TEST(VolumeControl, RememberedLevelOverridesSubsequentSharedChanges) {
  SharedVolumeLevel shared(AirPlayVolume{-24});
  VolumeControl first, second;
  shared.remember(AirPlayVolume{-15});
  first.rememberLevel(AirPlayVolume{-30});
  shared.remember(AirPlayVolume{0});
  EXPECT_EQ(first.suggestedLevel(shared), AirPlayVolume{-30});
  EXPECT_EQ(second.suggestedLevel(shared), AirPlayVolume{0});
}

TEST(VolumeControl, SoftwareGainAndUnmuteUpdatePcmSnapshot) {
  VolumeControl first;
  first.apply({.gainFixed16 = FixedGain16{1234}, .unmute = true}, false);
  EXPECT_EQ(first.pcmSnapshot().gainFixed16, FixedGain16{1234});
  EXPECT_FALSE(first.pcmSnapshot().softwareMuted);
}

TEST(VolumeControl, EmptyDecisionRetainsSoftwareMuteAndPreviousGain) {
  VolumeControl first;
  first.apply({.gainFixed16 = FixedGain16{1234}, .unmute = true}, false);
  first.apply({.requestMute = true}, false);
  EXPECT_TRUE(first.pcmSnapshot().softwareMuted);
  first.apply({}, false);
  EXPECT_TRUE(first.pcmSnapshot().softwareMuted);
  EXPECT_EQ(first.pcmSnapshot().gainFixed16, FixedGain16{1234});
}

TEST(VolumeControl, HardwareMuteRequestKeepsSoftwareUnmuted) {
  VolumeControl first;
  first.apply({.gainFixed16 = FixedGain16{1234}, .unmute = true}, false);
  first.apply({.requestMute = true}, false);
  first.apply({}, false);
  first.apply({.gainFixed16 = FixedGain16{65536}, .unmute = true}, false);
  first.apply({.requestMute = true}, true);
  EXPECT_FALSE(first.pcmSnapshot().softwareMuted);
}

TEST(VolumeControl, ConcurrentSnapshotsKeepGainAndMuteConsistent) {
  VolumeControl first;
  first.apply({.gainFixed16 = FixedGain16{1234}, .unmute = true}, false);
  first.apply({.requestMute = true}, false);
  first.apply({}, false);
  first.apply({.gainFixed16 = FixedGain16{65536}, .unmute = true}, false);
  first.apply({.requestMute = true}, true);
  std::thread setter([&] {
    for (int i = 0; i < 10000; ++i) {
      first.apply({.gainFixed16 = FixedGain16{11}, .requestMute = true}, false);
      first.apply({.gainFixed16 = FixedGain16{22}, .unmute = true}, false);
    }
  });
  for (int i = 0; i < 10000; ++i) {
    const auto pcm = first.pcmSnapshot();
    EXPECT_TRUE((pcm.gainFixed16 == FixedGain16{11} && pcm.softwareMuted) ||
                (pcm.gainFixed16 == FixedGain16{22} && !pcm.softwareMuted) || pcm.gainFixed16 == FixedGain16{65536});
  }
  setter.join();
}
