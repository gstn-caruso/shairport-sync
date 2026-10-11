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

TEST(BufferedFlushPolicy, PlaybackClearPreservesImmediateWhileReceiverResetClearsAll) {
  BufferedFlushPolicy policy;
  policy.requestImmediate(20, 200);
  ASSERT_TRUE(policy.requestDeferred(10, 100, 30, 300));
  policy.clearDeferredForPlayback();
  auto current = policy.evaluate(true, 10, 0);
  EXPECT_TRUE(current.discardCurrent);
  ASSERT_EQ(current.events().size(), 2u);
  EXPECT_EQ(current.events()[0].kind, BufferedFlushPolicy::EventKind::immediateStarted);
  EXPECT_EQ(current.events()[1].kind, BufferedFlushPolicy::EventKind::immediateDiscard);
  ASSERT_TRUE(policy.requestDeferred(11, 100, 30, 300));
  policy.resetForBufferedReceiver();
  auto reset = policy.evaluate(true, 11, 0);
  EXPECT_FALSE(reset.discardCurrent);
  EXPECT_TRUE(reset.events().empty());
}

TEST(BufferedFlushPolicy, ImmediateOverrunCompletesBeforeCancellingDeferredRequests) {
  BufferedFlushPolicy policy;
  ASSERT_TRUE(policy.requestDeferred(10, 100, 30, 300));
  ASSERT_TRUE(policy.requestDeferred(20, 200, 40, 400));
  EXPECT_TRUE(policy.evaluate(true, 10, 0).discardCurrent);
  policy.requestImmediate(15, 150);
  auto overrun = policy.evaluate(true, 16, 999);
  EXPECT_FALSE(overrun.discardCurrent);
  ASSERT_EQ(overrun.events().size(), 4u);
  EXPECT_EQ(overrun.events()[0].kind, BufferedFlushPolicy::EventKind::immediateStarted);
  EXPECT_EQ(overrun.events()[1].kind, BufferedFlushPolicy::EventKind::immediateOverrun);
  EXPECT_EQ(overrun.events()[2].kind, BufferedFlushPolicy::EventKind::immediateCompleted);
  EXPECT_EQ(overrun.events()[3].kind, BufferedFlushPolicy::EventKind::deferredCancelled);
  EXPECT_EQ(overrun.events()[3].fromSequence, 20u);
  EXPECT_FALSE(policy.evaluate(true, 20, 0).discardCurrent);
  EXPECT_TRUE(policy.evaluate(true, 21, 0).events().empty());
}

TEST(BufferedFlushPolicy, MissedDeferredStartRetainsBlocksUntilOverrunFreesSlot) {
  BufferedFlushPolicy policy;
  ASSERT_TRUE(policy.requestDeferred(10, 100, 20, 200));
  EXPECT_FALSE(policy.evaluate(true, 11, 100).discardCurrent);
  auto overrun = policy.evaluate(true, 21, 100);
  EXPECT_FALSE(overrun.discardCurrent);
  ASSERT_EQ(overrun.events().size(), 1u);
  EXPECT_EQ(overrun.events()[0].kind, BufferedFlushPolicy::EventKind::deferredOverrun);
  EXPECT_FALSE(overrun.events()[0].immediateWasActive);
  EXPECT_TRUE(policy.evaluate(true, 21, 100).events().empty());
}

TEST(BufferedFlushPolicy, EqualDeferredEndpointsCompleteWithoutDiscarding) {
  BufferedFlushPolicy policy;
  ASSERT_TRUE(policy.requestDeferred(10, 100, 10, 200));
  auto endpoint = policy.evaluate(true, 10, 0);
  EXPECT_FALSE(endpoint.discardCurrent);
  ASSERT_EQ(endpoint.events().size(), 1u);
  EXPECT_EQ(endpoint.events()[0].kind, BufferedFlushPolicy::EventKind::deferredCompleted);
}

TEST(BufferedFlushPolicy, TenSlotsBoundEventsAndEleventhAdmissionIsIgnored) {
  BufferedFlushPolicy policy;
  for (unsigned index = 0; index < 10; ++index)
    ASSERT_TRUE(policy.requestDeferred(10, index, 20, 200));
  EXPECT_FALSE(policy.requestDeferred(10, 999, 30, 300));
  policy.requestImmediate(30, 300);
  auto start = policy.evaluate(true, 10, 100);
  EXPECT_TRUE(start.discardCurrent);
  ASSERT_EQ(start.events().size(), 22u);
  for (unsigned index = 0; index < 10; ++index) {
    EXPECT_EQ(start.events()[2 + 2 * index].kind, BufferedFlushPolicy::EventKind::deferredActivated);
    EXPECT_EQ(start.events()[2 + 2 * index].fromTimestamp, index);
    EXPECT_EQ(start.events()[3 + 2 * index].kind, BufferedFlushPolicy::EventKind::deferredDiscard);
  }
  auto end = policy.evaluate(true, 20, 100);
  EXPECT_EQ(end.events().size(), 11u);
  EXPECT_TRUE(policy.requestDeferred(40, 400, 50, 500));
}

TEST(BufferedFlushPolicy, MaskedSequenceRequestsFollowTwentyThreeBitWraparound) {
  BufferedFlushPolicy policy;
  policy.requestImmediate(0x800001, 200);
  EXPECT_TRUE(policy.evaluate(true, 0x7fffff, 0).discardCurrent);
  EXPECT_TRUE(policy.evaluate(true, 0, 0).discardCurrent);
  EXPECT_FALSE(policy.evaluate(true, 1, 0).discardCurrent);
  ASSERT_TRUE(policy.requestDeferred(0xffffff, 100, 0x800001, 200));
  EXPECT_TRUE(policy.evaluate(true, 0x7fffff, 0).discardCurrent);
  EXPECT_TRUE(policy.evaluate(true, 0, 0).discardCurrent);
  EXPECT_FALSE(policy.evaluate(true, 1, 0).discardCurrent);
}

TEST(BufferedFlushPolicy, DeferredRequestsEvaluateBeforeAnyBlockWhileImmediateWaits) {
  BufferedFlushPolicy policy;
  policy.requestImmediate(20, 200);
  ASSERT_TRUE(policy.requestDeferred(10, 100, 30, 300));
  auto unread = policy.evaluate(false, 10, 0);
  EXPECT_TRUE(unread.discardCurrent);
  ASSERT_EQ(unread.events().size(), 2u);
  EXPECT_EQ(unread.events()[0].kind, BufferedFlushPolicy::EventKind::deferredActivated);
  auto endpoint = policy.evaluate(true, 20, 0);
  EXPECT_FALSE(endpoint.discardCurrent);
  ASSERT_EQ(endpoint.events().size(), 2u);
  EXPECT_EQ(endpoint.events()[0].kind, BufferedFlushPolicy::EventKind::immediateStarted);
  EXPECT_EQ(endpoint.events()[1].kind, BufferedFlushPolicy::EventKind::immediateCompleted);
}
