#include "volume/volume_policy.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <initializer_list>
#include <limits>

TEST(VolumePolicy, NonfiniteLevelsSelectMinimumAttenuationWithoutRequestingMute) {
  for (const double level : {std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::infinity(),
                            -std::numeric_limits<double>::infinity()}) {
    SCOPED_TRACE(level);
    const auto plan = VolumePolicy::plan(AirPlayVolume{level}, {}, {});
    EXPECT_EQ(plan.softwareAttenuation, CentibelAttenuation{-9630});
    EXPECT_EQ(plan.gainFixed16, FixedGain16{1});
    EXPECT_FALSE(plan.requestMute);
    EXPECT_TRUE(plan.unmute);
  }
}

TEST(VolumePolicy, OnlyTheExactMuteSentinelRequestsMute) {
  const auto plan = VolumePolicy::plan(AirPlayVolume{-144.000001}, {}, {});
  EXPECT_EQ(plan.softwareAttenuation, CentibelAttenuation{-9630});
  EXPECT_FALSE(plan.requestMute);
  EXPECT_TRUE(plan.unmute);
}

TEST(VolumePolicy, StandardProfileRetainsIntegerHalfSlopeForOddHardwareRange) {
  const auto plan = VolumePolicy::plan(AirPlayVolume{-15}, {}, {{VolumeRange{CentibelAttenuation{-4001}, CentibelAttenuation{0}}}, true});
  EXPECT_NEAR(plan.hardwareAttenuation->value(), -1200.2, 1e-9);
}

TEST(VolumePolicy, SoftwareOnlyFractionalLimitsPreserveTruncation) {
  VolumeSettings settings;
  settings.maximumDb = Decibels{-6.75};
  settings.rangeDb = Decibels{20.009};
  EXPECT_EQ(VolumePolicy::plan(AirPlayVolume{0}, settings, {}).softwareAttenuation, CentibelAttenuation{-600});
  EXPECT_EQ(VolumePolicy::plan(AirPlayVolume{-30}, settings, {}).softwareAttenuation, CentibelAttenuation{-2600});
}

TEST(VolumePolicy, HardwareFractionalMaximumTruncatesBeforeScaling) {
  VolumeSettings settings;
  settings.maximumDb = Decibels{-6.75};
  OutputVolumeCapabilities hardware{{VolumeRange{CentibelAttenuation{-4000}, CentibelAttenuation{0}}}, true};
  auto plan = VolumePolicy::plan(AirPlayVolume{0}, settings, hardware);
  EXPECT_EQ(plan.hardwareAttenuation, CentibelAttenuation{-600});
  EXPECT_EQ(plan.gainFixed16, FixedGain16{65536});
  EXPECT_FALSE(plan.maximumIgnored);
}

TEST(VolumePolicy, OutsideHardwareMaximumWithoutRangeIsReported) {
  VolumeSettings settings;
  settings.maximumDb = Decibels{-50};
  OutputVolumeCapabilities hardware{{VolumeRange{CentibelAttenuation{-4000}, CentibelAttenuation{0}}}, true};
  auto plan = VolumePolicy::plan(AirPlayVolume{0}, settings, hardware);
  EXPECT_TRUE(plan.maximumIgnored);
  EXPECT_EQ(plan.hardwareAttenuation, CentibelAttenuation{0});
  EXPECT_EQ(plan.gainFixed16, FixedGain16{65536});
}

TEST(VolumePolicy, OutsideHardwareMaximumWithRangeUsesSoftwareHeadroom) {
  VolumeSettings settings;
  settings.maximumDb = Decibels{-50};
  settings.rangeDb = Decibels{60};
  settings.profile = VolumeProfile::flat;
  OutputVolumeCapabilities hardware{{VolumeRange{CentibelAttenuation{-4000}, CentibelAttenuation{0}}}, true};
  auto plan = VolumePolicy::plan(AirPlayVolume{-15}, settings, hardware);
  EXPECT_EQ(plan.hardwareAttenuation, CentibelAttenuation{-4000});
  EXPECT_EQ(plan.softwareAttenuation, CentibelAttenuation{-4000});
  EXPECT_EQ(plan.scaledAttenuation, CentibelAttenuation{3000});
  EXPECT_FALSE(plan.maximumIgnored);
  EXPECT_FALSE(plan.rangeIgnored);
}

TEST(VolumePolicy, OversizedSoftwareRangeRetainsAvailableRange) {
  VolumeSettings settings;
  settings.rangeDb = Decibels{100};
  auto plan = VolumePolicy::plan(AirPlayVolume{-30}, settings, {});
  EXPECT_TRUE(plan.rangeIgnored);
  EXPECT_EQ(plan.softwareAttenuation, CentibelAttenuation{-9630});
  EXPECT_EQ(VolumePolicy::plan(AirPlayVolume{0}, settings, {}).softwareAttenuation, CentibelAttenuation{0});
}

TEST(VolumePolicy, OversizedMixedRangeReportsIgnoredRequest) {
  VolumeSettings settings;
  settings.rangeDb = Decibels{200};
  settings.profile = VolumeProfile::flat;
  OutputVolumeCapabilities hardware{{VolumeRange{CentibelAttenuation{-4000}, CentibelAttenuation{0}}}, true};
  auto plan = VolumePolicy::plan(AirPlayVolume{-15}, settings, hardware);
  EXPECT_TRUE(plan.rangeIgnored);
  EXPECT_EQ(plan.scaledAttenuation, CentibelAttenuation{6815});
  EXPECT_EQ(plan.hardwareAttenuation, CentibelAttenuation{-4000});
  EXPECT_EQ(plan.softwareAttenuation, CentibelAttenuation{-2815});
  EXPECT_EQ(plan.gainFixed16, FixedGain16{int(65536 * std::pow(10, -2815.0 / 2000))});
}

TEST(VolumePolicy, HardwareRangeWithoutSetterEmitsNoGainDecision) {
  OutputVolumeCapabilities hardware{{VolumeRange{CentibelAttenuation{-4000}, CentibelAttenuation{0}}}, false};
  auto plan = VolumePolicy::plan(AirPlayVolume{-15}, {}, hardware);
  EXPECT_FALSE(plan.hardwareAttenuation);
  EXPECT_FALSE(plan.gainFixed16);
  EXPECT_EQ(plan.softwareAttenuation, CentibelAttenuation{0});
  EXPECT_TRUE(plan.unmute);
}

TEST(VolumePolicy, HardwareSetterWithoutRangeUsesSoftwareGain) {
  VolumeSettings settings;
  settings.profile = VolumeProfile::flat;
  OutputVolumeCapabilities output{{}, true};
  auto plan = VolumePolicy::plan(AirPlayVolume{-15}, settings, output);
  EXPECT_FALSE(plan.hardwareAttenuation);
  EXPECT_EQ(plan.softwareAttenuation, CentibelAttenuation{-4815});
  EXPECT_EQ(plan.gainFixed16, FixedGain16{int(65536 * std::pow(10, -4815.0 / 2000))});
  EXPECT_TRUE(plan.unmute);
}

TEST(VolumePolicy, MuteRetainsRangeWarningWithoutGainChanges) {
  VolumeSettings settings;
  settings.maximumDb = Decibels{-50};
  OutputVolumeCapabilities hardware{{VolumeRange{CentibelAttenuation{-4000}, CentibelAttenuation{0}}}, true};
  auto plan = VolumePolicy::plan(AirPlayVolume{-144}, settings, hardware);
  EXPECT_TRUE(plan.maximumIgnored);
  EXPECT_TRUE(plan.requestMute);
  EXPECT_FALSE(plan.hardwareAttenuation);
  EXPECT_FALSE(plan.gainFixed16);
  EXPECT_FALSE(plan.unmute);
  EXPECT_EQ(plan.scaledAttenuation, CentibelAttenuation{0});
  EXPECT_EQ(plan.softwareAttenuation, CentibelAttenuation{0});
}

TEST(VolumePolicy, ProfilesPreserveAttenuationAndFixedGain) {
  VolumeSettings settings;
  settings.rangeDb = Decibels{60};
  for (auto profile : {VolumeProfile::standard, VolumeProfile::flat, VolumeProfile::dasl}) {
    settings.profile = profile;
    auto plan = VolumePolicy::plan(AirPlayVolume{-15}, settings, {});
    const double expected = profile == VolumeProfile::standard ? -1800 :
                            profile == VolumeProfile::flat ? -3000 : -1000;
    SCOPED_TRACE(static_cast<int>(profile));
    EXPECT_LT(std::abs(plan.softwareAttenuation.value() - expected), 1e-9);
    EXPECT_EQ(plan.gainFixed16, FixedGain16{int(65536 * std::pow(10, plan.softwareAttenuation.value() / 2000))});
    EXPECT_EQ(VolumePolicy::plan(AirPlayVolume{0}, settings, {}).softwareAttenuation, CentibelAttenuation{0});
    EXPECT_EQ(VolumePolicy::plan(AirPlayVolume{-30}, settings, {}).softwareAttenuation, CentibelAttenuation{-6000});
    EXPECT_EQ(VolumePolicy::plan(AirPlayVolume{1}, settings, {}).softwareAttenuation, CentibelAttenuation{-6000});
    EXPECT_EQ(VolumePolicy::plan(AirPlayVolume{-31}, settings, {}).softwareAttenuation, CentibelAttenuation{-6000});
  }
}

TEST(VolumePolicy, HardwarePriorityUsesHardwareBeforeSoftwareAttenuation) {
  VolumeSettings settings;
  settings.rangeDb = Decibels{60};
  settings.profile = VolumeProfile::flat;
  OutputVolumeCapabilities hardware{{VolumeRange{CentibelAttenuation{-4000}, CentibelAttenuation{0}}}, true};
  auto mixed = VolumePolicy::plan(AirPlayVolume{-15}, settings, hardware);
  EXPECT_EQ(mixed.hardwareAttenuation, CentibelAttenuation{-3000});
  EXPECT_EQ(mixed.softwareAttenuation, CentibelAttenuation{0});
}

TEST(VolumePolicy, SoftwarePriorityUsesSoftwareBeforeHardwareAttenuation) {
  VolumeSettings settings;
  settings.rangeDb = Decibels{60};
  settings.profile = VolumeProfile::flat;
  settings.hardwarePriority = false;
  OutputVolumeCapabilities hardware{{VolumeRange{CentibelAttenuation{-4000}, CentibelAttenuation{0}}}, true};
  auto mixed = VolumePolicy::plan(AirPlayVolume{-15}, settings, hardware);
  EXPECT_EQ(mixed.hardwareAttenuation, CentibelAttenuation{-1000});
  EXPECT_EQ(mixed.softwareAttenuation, CentibelAttenuation{-2000});
}

TEST(VolumePolicy, HardwareOnlyRangeKeepsUnitySoftwareGain) {
  VolumeSettings settings;
  settings.profile = VolumeProfile::flat;
  settings.hardwarePriority = false;
  settings.rangeDb = Decibels{20};
  OutputVolumeCapabilities hardware{{VolumeRange{CentibelAttenuation{-4000}, CentibelAttenuation{0}}}, true};
  auto hardwareOnly = VolumePolicy::plan(AirPlayVolume{-15}, settings, hardware);
  EXPECT_EQ(hardwareOnly.hardwareAttenuation, CentibelAttenuation{-1000});
  EXPECT_EQ(hardwareOnly.gainFixed16, FixedGain16{65536});
}

TEST(VolumePolicy, MuteLevelRequestsMute) {
  VolumeSettings settings;
  settings.profile = VolumeProfile::flat;
  settings.hardwarePriority = false;
  settings.rangeDb = Decibels{20};
  OutputVolumeCapabilities hardware{{VolumeRange{CentibelAttenuation{-4000}, CentibelAttenuation{0}}}, true};
  EXPECT_TRUE(VolumePolicy::plan(AirPlayVolume{-144}, settings, hardware).requestMute);
}

TEST(VolumePolicy, IgnoredControlDoesNotMuteOrChangeGain) {
  VolumeSettings settings;
  settings.profile = VolumeProfile::flat;
  settings.hardwarePriority = false;
  settings.rangeDb = Decibels{20};
  OutputVolumeCapabilities hardware{{VolumeRange{CentibelAttenuation{-4000}, CentibelAttenuation{0}}}, true};
  settings.ignoreControl = true;
  auto ignored = VolumePolicy::plan(AirPlayVolume{-144}, settings, hardware);
  EXPECT_FALSE(ignored.requestMute);
  EXPECT_FALSE(ignored.gainFixed16);
  EXPECT_FALSE(ignored.unmute);
  EXPECT_EQ(VolumePolicy::plan(AirPlayVolume{-15}, settings, hardware).hardwareAttenuation, CentibelAttenuation{0});
}
