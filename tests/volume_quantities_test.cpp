#include "volume/volume_quantities.hpp"
#include <gtest/gtest.h>
#include <limits>
#include <type_traits>

static_assert(!std::is_convertible_v<double, AirPlayVolume>);
static_assert(!std::is_convertible_v<AirPlayVolume, double>);
static_assert(!std::is_convertible_v<Decibels, AirPlayVolume>);
static_assert(!std::is_convertible_v<CentibelAttenuation, Decibels>);
static_assert(!std::is_convertible_v<FixedGain16, int>);

TEST(VolumeQuantities, WireParametersPreserveFloatRoundingAndPermissiveConversion) {
  EXPECT_EQ(AirPlayVolume::fromWireParameter("-15.1234567"), AirPlayVolume{-15.123456954956055});
  EXPECT_EQ(AirPlayVolume::fromWireParameter("not-a-number"), AirPlayVolume{0});
  EXPECT_EQ(AirPlayVolume::fromWireParameter("  -12.5remaining"), AirPlayVolume{-12.5});
}

TEST(VolumeQuantities, ExactMuteAndPlayableRangeAreDifferentDecisions) {
  EXPECT_TRUE(AirPlayVolume{-144}.isMute());
  EXPECT_FALSE(AirPlayVolume{-144.000001}.isMute());
  EXPECT_FALSE(AirPlayVolume{-144}.isPlayable());
  EXPECT_TRUE(AirPlayVolume{-30}.isPlayable());
  EXPECT_TRUE(AirPlayVolume{0}.isPlayable());
  EXPECT_FALSE(AirPlayVolume{1}.isPlayable());
  EXPECT_FALSE(AirPlayVolume{std::numeric_limits<double>::quiet_NaN()}.isPlayable());
}

TEST(VolumeQuantities, ConfiguredMaximumTruncatesWholeDecibelsBeforeCentibelScaling) {
  EXPECT_EQ(Decibels{-6.75}.maximumAttenuation(), CentibelAttenuation{-600});
  EXPECT_EQ(Decibels{20.009}.inCentibels().wholeCentibels(), CentibelAttenuation{2000});
}

TEST(VolumeQuantities, AttenuationProducesTruncatedFixedPointAmplitude) {
  EXPECT_EQ(CentibelAttenuation{0}.fixedGain(), FixedGain16::unity());
  EXPECT_EQ(CentibelAttenuation{-9630}.fixedGain(), FixedGain16{1});
}
