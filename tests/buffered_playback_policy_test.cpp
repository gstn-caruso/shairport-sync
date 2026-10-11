#include <gtest/gtest.h>
import receiver.protocol.ap2.buffered_playback;

TEST(BufferedPlaybackPolicy, PlayTransitionsRequestFreshBlockAndResetOnlyOnStop) {
  BufferedPlaybackPolicy policy(0.25);
  EXPECT_FALSE(policy.onPlayState(false).resetPlayer);
  auto start = policy.onPlayState(true);
  EXPECT_TRUE(start.started);
  EXPECT_TRUE(start.needFreshBlock);
  EXPECT_FALSE(start.resetPlayer);
  EXPECT_FALSE(policy.onPlayState(true).started);
  auto stop = policy.onPlayState(false);
  EXPECT_TRUE(stop.stopped);
  EXPECT_TRUE(stop.resetPlayer);
  EXPECT_FALSE(stop.needFreshBlock);
  EXPECT_FALSE(policy.onPlayState(false).stopped);
}
