#include "volume/volume_policy.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <initializer_list>

TEST(VolumePolicy, SoftwareOnlyFractionalLimitsPreserveTruncation) {
  VolumeSettings settings;
  settings.maximumDb = -6.75;
  settings.rangeDb = 20.009;
  EXPECT_EQ(VolumePolicy::plan(0, settings, {}).softwareAttenuation, -600);
  EXPECT_EQ(VolumePolicy::plan(-30, settings, {}).softwareAttenuation, -2600);
}

TEST(VolumePolicy, HardwareFractionalMaximumTruncatesBeforeScaling) {
  VolumeSettings settings;
  settings.maximumDb = -6.75;
  OutputVolumeCapabilities hardware{{VolumeRange{-4000, 0}}, true};
  auto plan = VolumePolicy::plan(0, settings, hardware);
  EXPECT_EQ(plan.hardwareAttenuation, -600);
  EXPECT_EQ(plan.gainFixed16, 65536);
  EXPECT_FALSE(plan.maximumIgnored);
}

TEST(VolumePolicy, OutsideHardwareMaximumWithoutRangeIsReported) {
  VolumeSettings settings;
  settings.maximumDb = -50;
  OutputVolumeCapabilities hardware{{VolumeRange{-4000, 0}}, true};
  auto plan = VolumePolicy::plan(0, settings, hardware);
  EXPECT_TRUE(plan.maximumIgnored);
  EXPECT_EQ(plan.hardwareAttenuation, 0);
  EXPECT_EQ(plan.gainFixed16, 65536);
}

TEST(VolumePolicy, OutsideHardwareMaximumWithRangeUsesSoftwareHeadroom) {
  VolumeSettings settings;
  settings.maximumDb = -50;
  settings.rangeDb = 60;
  settings.profile = VolumeProfile::flat;
  OutputVolumeCapabilities hardware{{VolumeRange{-4000, 0}}, true};
  auto plan = VolumePolicy::plan(-15, settings, hardware);
  EXPECT_EQ(plan.hardwareAttenuation, -4000);
  EXPECT_EQ(plan.softwareAttenuation, -4000);
  EXPECT_EQ(plan.scaledAttenuation, 3000);
  EXPECT_FALSE(plan.maximumIgnored);
  EXPECT_FALSE(plan.rangeIgnored);
}

TEST(VolumePolicy, OversizedSoftwareRangeRetainsAvailableRange) {
  VolumeSettings settings;
  settings.rangeDb = 100;
  auto plan = VolumePolicy::plan(-30, settings, {});
  EXPECT_TRUE(plan.rangeIgnored);
  EXPECT_EQ(plan.softwareAttenuation, -9630);
  EXPECT_EQ(VolumePolicy::plan(0, settings, {}).softwareAttenuation, 0);
}

TEST(VolumePolicy, OversizedMixedRangeReportsIgnoredRequest) {
  VolumeSettings settings;
  settings.rangeDb = 200;
  settings.profile = VolumeProfile::flat;
  OutputVolumeCapabilities hardware{{VolumeRange{-4000, 0}}, true};
  auto plan = VolumePolicy::plan(-15, settings, hardware);
  EXPECT_TRUE(plan.rangeIgnored);
  EXPECT_EQ(plan.scaledAttenuation, 6815);
  EXPECT_EQ(plan.hardwareAttenuation, -4000);
  EXPECT_EQ(plan.softwareAttenuation, -2815);
  EXPECT_EQ(plan.gainFixed16, int(65536 * std::pow(10, -2815.0 / 2000)));
}

TEST(VolumePolicy, HardwareRangeWithoutSetterEmitsNoGainDecision) {
  OutputVolumeCapabilities hardware{{VolumeRange{-4000, 0}}, false};
  auto plan = VolumePolicy::plan(-15, {}, hardware);
  EXPECT_FALSE(plan.hardwareAttenuation);
  EXPECT_FALSE(plan.gainFixed16);
  EXPECT_EQ(plan.softwareAttenuation, 0);
  EXPECT_TRUE(plan.unmute);
}

TEST(VolumePolicy, HardwareSetterWithoutRangeUsesSoftwareGain) {
  VolumeSettings settings;
  settings.profile = VolumeProfile::flat;
  OutputVolumeCapabilities output{{}, true};
  auto plan = VolumePolicy::plan(-15, settings, output);
  EXPECT_FALSE(plan.hardwareAttenuation);
  EXPECT_EQ(plan.softwareAttenuation, -4815);
  EXPECT_EQ(plan.gainFixed16, int(65536 * std::pow(10, -4815.0 / 2000)));
  EXPECT_TRUE(plan.unmute);
}

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
