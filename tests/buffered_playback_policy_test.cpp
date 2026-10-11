#include <gtest/gtest.h>
#include <optional>
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
TEST(BufferedPlaybackPolicy, MissingClockWaitsWithoutPacketRateAndStrictLeadControlsAdmission) {
  BufferedPlaybackPolicy policy(0.25);
  policy.onPlayState(true);
  auto missing = policy.admit(std::nullopt, 0, 0, 0);
  EXPECT_EQ(missing.kind, BufferedAdmissionKind::waitClock);
  EXPECT_EQ(missing.waitUs, 20000u);
  auto boundary = policy.admit(1350000000, 1000000000, 1024, 48000);
  EXPECT_EQ(boundary.kind, BufferedAdmissionKind::waitPacket);
  EXPECT_EQ(boundary.waitUs, 42666u);
  EXPECT_EQ(policy.admit(1349999999, 1000000000, 1024, 48000).kind, BufferedAdmissionKind::prepare);
  EXPECT_EQ(policy.admit(1000000000, 1000000000, 1024, 48000).kind, BufferedAdmissionKind::prepare);
  auto late = policy.admit(999999999, 1000000000, 1024, 48000);
  EXPECT_EQ(late.kind, BufferedAdmissionKind::consumeLate);
  EXPECT_EQ(late.leadNs, -1);
}
TEST(BufferedPlaybackPolicy, DisabledPlayStillWarnsOnceAndAdmissionRearmsWarning) {
  BufferedPlaybackPolicy policy(0.25);
  EXPECT_FALSE(policy.admit(1450000000, 1000000000, 352, 44100).warnEarly);
  auto early = policy.admit(1450000001, 1000000000, 352, 44100);
  EXPECT_EQ(early.kind, BufferedAdmissionKind::waitPacket);
  EXPECT_TRUE(early.warnEarly);
  EXPECT_EQ(early.waitUs, 15962u);
  EXPECT_FALSE(policy.admit(1450000001, 1000000000, 352, 44100).warnEarly);
  policy.onPlayState(true);
  EXPECT_EQ(policy.admit(1100000000, 1000000000, 352, 44100).kind, BufferedAdmissionKind::prepare);
  EXPECT_TRUE(policy.admit(1450000001, 1000000000, 352, 44100).warnEarly);
}
