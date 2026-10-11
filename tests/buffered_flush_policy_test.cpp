#include "protocol/ap2/buffered_flush_policy.hpp"
#include <gtest/gtest.h>

TEST(BufferedFlushPolicy, EmptyPolicyRetainsCurrentBlockWithoutEvents) {
  BufferedFlushPolicy policy;
  auto decision = policy.evaluate(true, 12, 100);
  EXPECT_FALSE(decision.discardCurrent);
  EXPECT_TRUE(decision.events().empty());
}

TEST(BufferedFlushPolicy, ImmediateFlushDiscardsBeforeEndpointAndRetainsEndpoint) {
  BufferedFlushPolicy policy;
  policy.requestImmediate(20, 200);
  auto before = policy.evaluate(true, 19, 999);
  EXPECT_TRUE(before.discardCurrent);
  ASSERT_EQ(before.events().size(), 2u);
  EXPECT_EQ(before.events()[0].kind, BufferedFlushPolicy::EventKind::immediateStarted);
  EXPECT_EQ(before.events()[1].kind, BufferedFlushPolicy::EventKind::immediateDiscard);
  EXPECT_EQ(before.events()[1].untilSequence, 20u);
  auto repeat = policy.evaluate(true, 19, 999);
  EXPECT_TRUE(repeat.discardCurrent);
  ASSERT_EQ(repeat.events().size(), 1u);
  EXPECT_EQ(repeat.events()[0].kind, BufferedFlushPolicy::EventKind::immediateDiscard);
  auto endpoint = policy.evaluate(true, 20, 0);
  EXPECT_FALSE(endpoint.discardCurrent);
  ASSERT_EQ(endpoint.events().size(), 1u);
  EXPECT_EQ(endpoint.events()[0].kind, BufferedFlushPolicy::EventKind::immediateCompleted);
  EXPECT_TRUE(policy.evaluate(true, 20, 0).events().empty());
}

TEST(BufferedFlushPolicy, DeferredFlushActivatesAtStartAndRetainsEndpoint) {
  BufferedFlushPolicy policy;
  ASSERT_TRUE(policy.requestDeferred(10, 100, 20, 200));
  EXPECT_FALSE(policy.evaluate(true, 9, 999).discardCurrent);
  auto start = policy.evaluate(true, 10, 0);
  EXPECT_TRUE(start.discardCurrent);
  ASSERT_EQ(start.events().size(), 2u);
  EXPECT_EQ(start.events()[0].kind, BufferedFlushPolicy::EventKind::deferredActivated);
  EXPECT_EQ(start.events()[1].kind, BufferedFlushPolicy::EventKind::deferredDiscard);
  EXPECT_EQ(start.events()[0].fromTimestamp, 100u);
  EXPECT_EQ(start.events()[0].untilTimestamp, 200u);
  EXPECT_TRUE(policy.evaluate(true, 19, 0).discardCurrent);
  auto end = policy.evaluate(true, 20, 999);
  EXPECT_FALSE(end.discardCurrent);
  ASSERT_EQ(end.events().size(), 1u);
  EXPECT_EQ(end.events()[0].kind, BufferedFlushPolicy::EventKind::deferredCompleted);
  EXPECT_TRUE(policy.evaluate(true, 20, 999).events().empty());
}
