#include "volume/volume_control.hpp"
#include <gtest/gtest.h>
#include <thread>

TEST(VolumeControl, UnrememberedControlsFollowSharedLevel) {
  SharedVolumeLevel shared(-24);
  VolumeControl first, second;
  EXPECT_EQ(first.suggestedLevel(shared), -24);
  shared.remember(-15);
  EXPECT_EQ(first.suggestedLevel(shared), -15);
  EXPECT_EQ(second.suggestedLevel(shared), -15);
}

TEST(VolumeControl, RememberedLevelOverridesSubsequentSharedChanges) {
  SharedVolumeLevel shared(-24);
  VolumeControl first, second;
  shared.remember(-15);
  first.rememberLevel(-30);
  shared.remember(0);
  EXPECT_EQ(first.suggestedLevel(shared), -30);
  EXPECT_EQ(second.suggestedLevel(shared), 0);
}

TEST(VolumeControl, SoftwareGainAndUnmuteUpdatePcmSnapshot) {
  VolumeControl first;
  first.apply({.gainFixed16 = 1234, .unmute = true}, false);
  EXPECT_EQ(first.pcmSnapshot().gainFixed16, 1234);
  EXPECT_FALSE(first.pcmSnapshot().softwareMuted);
}

TEST(VolumeControl, EmptyDecisionRetainsSoftwareMuteAndPreviousGain) {
  VolumeControl first;
  first.apply({.gainFixed16 = 1234, .unmute = true}, false);
  first.apply({.requestMute = true}, false);
  EXPECT_TRUE(first.pcmSnapshot().softwareMuted);
  first.apply({}, false);
  EXPECT_TRUE(first.pcmSnapshot().softwareMuted);
  EXPECT_EQ(first.pcmSnapshot().gainFixed16, 1234);
}

TEST(VolumeControl, HardwareMuteRequestKeepsSoftwareUnmuted) {
  VolumeControl first;
  first.apply({.gainFixed16 = 1234, .unmute = true}, false);
  first.apply({.requestMute = true}, false);
  first.apply({}, false);
  first.apply({.gainFixed16 = 65536, .unmute = true}, false);
  first.apply({.requestMute = true}, true);
  EXPECT_FALSE(first.pcmSnapshot().softwareMuted);
}

TEST(VolumeControl, ConcurrentSnapshotsKeepGainAndMuteConsistent) {
  VolumeControl first;
  first.apply({.gainFixed16 = 1234, .unmute = true}, false);
  first.apply({.requestMute = true}, false);
  first.apply({}, false);
  first.apply({.gainFixed16 = 65536, .unmute = true}, false);
  first.apply({.requestMute = true}, true);
  std::thread setter([&] {
    for (int i = 0; i < 10000; ++i) {
      first.apply({.gainFixed16 = 11, .requestMute = true}, false);
      first.apply({.gainFixed16 = 22, .unmute = true}, false);
    }
  });
  for (int i = 0; i < 10000; ++i) {
    const auto pcm = first.pcmSnapshot();
    EXPECT_TRUE((pcm.gainFixed16 == 11 && pcm.softwareMuted) ||
                (pcm.gainFixed16 == 22 && !pcm.softwareMuted) || pcm.gainFixed16 == 65536);
  }
  setter.join();
}
