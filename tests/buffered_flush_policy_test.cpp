#include "protocol/ap2/buffered_flush_policy.hpp"
#include <gtest/gtest.h>

TEST(BufferedFlushPolicy, EmptyPolicyRetainsCurrentBlockWithoutEvents) {
  BufferedFlushPolicy policy;
  auto decision = policy.evaluate(true, 12, 100);
  EXPECT_FALSE(decision.discardCurrent);
  EXPECT_TRUE(decision.events().empty());
}
