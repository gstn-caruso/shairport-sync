#include "channel_mapping.hpp"
#include <array>
#include <gtest/gtest.h>

static void checkMapping(ChannelMapping::Specification specification, int16_t left, int16_t right) {
  auto mapping = ChannelMapping::from({"FL", "FR"}, 2, specification);
  const std::array<int16_t, 4> source{5, 9, -5, -9};
  std::array<int16_t, 4> output{};
  ASSERT_TRUE(mapping.map(source, output));
  EXPECT_EQ(output[0], left);
  EXPECT_EQ(output[1], right);
  EXPECT_EQ(output[2], -left);
  EXPECT_EQ(output[3], -right);
}

TEST(ChannelMapping, DefaultOrderPreservesStereoChannels) {
  checkMapping({}, 5, 9);
}

TEST(ChannelMapping, ExplicitNamesOverrideDeviceOrder) {
  checkMapping({true, {"FR", "FL"}, "FL FR"}, 9, 5);
}

TEST(ChannelMapping, DisabledMappingIgnoresExplicitNames) {
  checkMapping({false, {"FR", "FL"}, ""}, 5, 9);
}

TEST(ChannelMapping, DeviceNamesSupplyOrderWhenExplicitNamesAreEmpty) {
  checkMapping({true, {}, "FR FL"}, 9, 5);
}

TEST(ChannelMapping, UnknownNameUsesRemainingUnassignedSourceChannel) {
  checkMapping({true, {"UNKNOWN", "FL"}, ""}, 9, 5);
}

TEST(ChannelMapping, FrontMonoMixesChannelsAndSilenceProducesZero) {
  checkMapping({true, {"FM", "--"}, ""}, 6, 0);
}

TEST(ChannelMapping, FrontMonoDividesSignedSamplesBeforeSumming) {
  auto mono = ChannelMapping::from({"FL", "FR"}, 1, {true, {"FM"}, ""});
  const std::array<int32_t, 2> oddSigned{-5, 9};
  std::array<int32_t, 1> mixed{};
  ASSERT_TRUE(mono.map(oddSigned, mixed));
  EXPECT_EQ(mixed[0], 2);
}

TEST(ChannelMapping, MixedSelectionsMapEveryFrameInOutputOrder) {
  auto mapping = ChannelMapping::from({"FL", "FR"}, 4, {true, {"FR", "FM", "--", "FL"}, ""});
  const std::array<int32_t, 6> input{5, 9, -5, -9, -5, 9};
  std::array<int32_t, 12> output{};
  const std::array<int32_t, 12> expected{9, 6, 0, 5, -9, -6, 0, -5, 9, 2, 0, -5};

  ASSERT_TRUE(mapping.map(input, output));
  EXPECT_EQ(output, expected);
}

TEST(ChannelMapping, UnknownDeviceNameMarksMappingIncomplete) {
  auto incomplete = ChannelMapping::from({"FL", "FR"}, 2, {true, {}, "UNKNOWN FR"});
  EXPECT_TRUE(incomplete.isIncomplete());
}

TEST(ChannelMapping, ShortInputIsRejectedWithoutChangingOutput) {
  auto incomplete = ChannelMapping::from({"FL", "FR"}, 2, {true, {}, "UNKNOWN FR"});
  const std::array<int16_t, 1> shortInput{7};
  std::array<int16_t, 2> untouched{11, 13};
  EXPECT_FALSE(incomplete.map(shortInput, untouched));
  EXPECT_EQ(untouched[0], 11);
  EXPECT_EQ(untouched[1], 13);
}
