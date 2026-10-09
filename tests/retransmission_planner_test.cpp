#include "packets/retransmission_planner.hpp"
#include <gtest/gtest.h>

static shairport::packets::RetransmissionPlanner missingPacketsAcrossWraparound() {
  shairport::packets::RetransmissionPlanner planner;
  planner.noteMissing(65535, 1000);
  planner.noteMissing(0, 1000);
  planner.noteMissing(2, 1000);
  return planner;
}

TEST(RetransmissionPlanner, InitialAgeBoundaryGroupsContiguousRangesAcrossWraparound) {
  auto planner = missingPacketsAcrossWraparound();
  const shairport::packets::RetryPolicy policy{100, 50, 20, 1000};
  EXPECT_TRUE(planner.due(1099, policy, {65535, 3}).empty());
  auto first = planner.due(1100, policy, {65535, 3});
  ASSERT_EQ(first.size(), 2);
  EXPECT_EQ(first[0].first, 65535);
  EXPECT_EQ(first[0].count, 2);
  EXPECT_EQ(first[1].first, 2);
  EXPECT_EQ(first[1].count, 1);
}

TEST(RetransmissionPlanner, RetryIntervalIncludesExactBoundary) {
  auto planner = missingPacketsAcrossWraparound();
  const shairport::packets::RetryPolicy policy{100, 50, 20, 1000};
  planner.due(1100, policy, {65535, 3});
  EXPECT_TRUE(planner.due(1149, policy, {65535, 3}).empty());
  auto repeat = planner.due(1150, policy, {65535, 3});
  EXPECT_EQ(repeat.size(), 2);
}

TEST(RetransmissionPlanner, RejectedChecksPreserveInitialAndRepeatedRequestBoundaries) {
  shairport::packets::RetransmissionPlanner planner;
  planner.noteMissing(7, 1000);
  const shairport::packets::RetryPolicy policy{100, 50, 20, 1000};
  const shairport::packets::PacketWindow window{7, 8};

  EXPECT_TRUE(planner.due(999, policy, window).empty());
  EXPECT_TRUE(planner.due(1099, policy, window).empty());
  const auto initial = planner.due(1100, policy, window);
  ASSERT_EQ(initial.size(), 1);
  EXPECT_EQ(initial[0].first, 7);
  EXPECT_EQ(initial[0].count, 1);

  EXPECT_TRUE(planner.due(1099, policy, window).empty());
  EXPECT_TRUE(planner.due(1149, policy, window).empty());
  const auto repeated = planner.due(1150, policy, window);
  ASSERT_EQ(repeated.size(), 1);
  EXPECT_EQ(repeated[0].first, 7);
  EXPECT_EQ(repeated[0].count, 1);
}

TEST(RetransmissionPlanner, ResolutionShrinksRangesAtFinalOpportunityBoundary) {
  auto planner = missingPacketsAcrossWraparound();
  const shairport::packets::RetryPolicy policy{100, 50, 20, 1000};
  planner.due(1100, policy, {65535, 3});
  planner.due(1150, policy, {65535, 3});
  planner.resolve(0);
  auto lastChance = planner.due(1980, policy, {65535, 3});
  ASSERT_EQ(lastChance.size(), 2);
  EXPECT_EQ(lastChance[0].count, 1);
  EXPECT_TRUE(planner.due(1981, policy, {65535, 3}).empty());
}

TEST(RetransmissionPlanner, ResetLeavesNoRetransmissionsDue) {
  auto planner = missingPacketsAcrossWraparound();
  const shairport::packets::RetryPolicy policy{100, 50, 20, 1000};
  planner.due(1100, policy, {65535, 3});
  planner.due(1150, policy, {65535, 3});
  planner.resolve(0);
  planner.due(1980, policy, {65535, 3});
  planner.due(1981, policy, {65535, 3});
  planner.reset();
  EXPECT_TRUE(planner.due(2000, policy, {65535, 3}).empty());
}

TEST(RetransmissionPlanner, EmptyWindowPreservesPendingRequest) {
  shairport::packets::RetransmissionPlanner planner;
  planner.noteMissing(65535, 1000);
  const shairport::packets::RetryPolicy policy{100, 50, 20, 1000};

  EXPECT_TRUE(planner.due(1100, policy, {65535, 65535}).empty());
  const auto ranges = planner.due(1100, policy, {65535, 0});
  ASSERT_EQ(ranges.size(), 1);
  EXPECT_EQ(ranges[0].first, 65535);
  EXPECT_EQ(ranges[0].count, 1);
}

TEST(RetransmissionPlanner, OversizedWindowPreservesMaximumWrappedRequest) {
  shairport::packets::RetransmissionPlanner planner;
  for (unsigned offset = 0; offset < 1024; ++offset)
    planner.noteMissing(static_cast<uint16_t>(65024 + offset), 1000);
  const shairport::packets::RetryPolicy policy{100, 50, 20, 1000};

  EXPECT_TRUE(planner.due(1100, policy, {65024, 513}).empty());
  const auto ranges = planner.due(1100, policy, {65024, 512});
  ASSERT_EQ(ranges.size(), 1);
  EXPECT_EQ(ranges[0].first, 65024);
  EXPECT_EQ(ranges[0].count, 1024);
}
