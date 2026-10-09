#include "retransmission_planner.hpp"
#include <gtest/gtest.h>

static RetransmissionPlanner missingPacketsAcrossWraparound() {
  RetransmissionPlanner planner;
  planner.noteMissing(65535, 1000);
  planner.noteMissing(0, 1000);
  planner.noteMissing(2, 1000);
  return planner;
}

TEST(RetransmissionPlanner, InitialAgeBoundaryGroupsContiguousRangesAcrossWraparound) {
  auto planner = missingPacketsAcrossWraparound();
  const RetryPolicy policy{100, 50, 20, 1000};
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
  const RetryPolicy policy{100, 50, 20, 1000};
  planner.due(1100, policy, {65535, 3});
  EXPECT_TRUE(planner.due(1149, policy, {65535, 3}).empty());
  auto repeat = planner.due(1150, policy, {65535, 3});
  EXPECT_EQ(repeat.size(), 2);
}

TEST(RetransmissionPlanner, ResolutionShrinksRangesAtFinalOpportunityBoundary) {
  auto planner = missingPacketsAcrossWraparound();
  const RetryPolicy policy{100, 50, 20, 1000};
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
  const RetryPolicy policy{100, 50, 20, 1000};
  planner.due(1100, policy, {65535, 3});
  planner.due(1150, policy, {65535, 3});
  planner.resolve(0);
  planner.due(1980, policy, {65535, 3});
  planner.due(1981, policy, {65535, 3});
  planner.reset();
  EXPECT_TRUE(planner.due(2000, policy, {65535, 3}).empty());
}
