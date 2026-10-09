#include "volume_policy.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <initializer_list>

TEST(VolumePolicy, ProfilesPreserveAttenuationAndFixedGain) {
  VolumeSettings settings;
  settings.rangeDb = 60;
  for (auto profile : {VolumeProfile::standard, VolumeProfile::flat, VolumeProfile::dasl}) {
    settings.profile = profile;
    auto plan = VolumePolicy::plan(-15, settings, {});
    const double expected = profile == VolumeProfile::standard ? -1800 :
                            profile == VolumeProfile::flat ? -3000 : -1000;
    SCOPED_TRACE(static_cast<int>(profile));
    EXPECT_LT(std::abs(plan.softwareAttenuation - expected), 1e-9);
    EXPECT_EQ(plan.gainFixed16, int(65536 * std::pow(10, plan.softwareAttenuation / 2000)));
    EXPECT_EQ(VolumePolicy::plan(0, settings, {}).softwareAttenuation, 0);
    EXPECT_EQ(VolumePolicy::plan(-30, settings, {}).softwareAttenuation, -6000);
    EXPECT_EQ(VolumePolicy::plan(1, settings, {}).softwareAttenuation, -6000);
    EXPECT_EQ(VolumePolicy::plan(-31, settings, {}).softwareAttenuation, -6000);
  }
}

TEST(VolumePolicy, HardwarePriorityUsesHardwareBeforeSoftwareAttenuation) {
  VolumeSettings settings;
  settings.rangeDb = 60;
  settings.profile = VolumeProfile::flat;
  OutputVolumeCapabilities hardware{{VolumeRange{-4000, 0}}, true};
  auto mixed = VolumePolicy::plan(-15, settings, hardware);
  EXPECT_EQ(mixed.hardwareAttenuation, -3000);
  EXPECT_EQ(mixed.softwareAttenuation, 0);
}

TEST(VolumePolicy, SoftwarePriorityUsesSoftwareBeforeHardwareAttenuation) {
  VolumeSettings settings;
  settings.rangeDb = 60;
  settings.profile = VolumeProfile::flat;
  settings.hardwarePriority = false;
  OutputVolumeCapabilities hardware{{VolumeRange{-4000, 0}}, true};
  auto mixed = VolumePolicy::plan(-15, settings, hardware);
  EXPECT_EQ(mixed.hardwareAttenuation, -1000);
  EXPECT_EQ(mixed.softwareAttenuation, -2000);
}

TEST(VolumePolicy, HardwareOnlyRangeKeepsUnitySoftwareGain) {
  VolumeSettings settings;
  settings.profile = VolumeProfile::flat;
  settings.hardwarePriority = false;
  settings.rangeDb = 20;
  OutputVolumeCapabilities hardware{{VolumeRange{-4000, 0}}, true};
  auto hardwareOnly = VolumePolicy::plan(-15, settings, hardware);
  EXPECT_EQ(hardwareOnly.hardwareAttenuation, -1000);
  EXPECT_EQ(hardwareOnly.gainFixed16, 65536);
}

TEST(VolumePolicy, MuteLevelRequestsMute) {
  VolumeSettings settings;
  settings.profile = VolumeProfile::flat;
  settings.hardwarePriority = false;
  settings.rangeDb = 20;
  OutputVolumeCapabilities hardware{{VolumeRange{-4000, 0}}, true};
  EXPECT_TRUE(VolumePolicy::plan(-144, settings, hardware).requestMute);
}

TEST(VolumePolicy, IgnoredControlDoesNotMuteOrChangeGain) {
  VolumeSettings settings;
  settings.profile = VolumeProfile::flat;
  settings.hardwarePriority = false;
  settings.rangeDb = 20;
  OutputVolumeCapabilities hardware{{VolumeRange{-4000, 0}}, true};
  settings.ignoreControl = true;
  auto ignored = VolumePolicy::plan(-144, settings, hardware);
  EXPECT_FALSE(ignored.requestMute);
  EXPECT_FALSE(ignored.gainFixed16);
  EXPECT_FALSE(ignored.unmute);
  EXPECT_EQ(VolumePolicy::plan(-15, settings, hardware).hardwareAttenuation, 0);
}
