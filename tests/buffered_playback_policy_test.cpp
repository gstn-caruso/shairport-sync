#include <gtest/gtest.h>
#include <optional>
#include <cstdint>
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
TEST(BufferedPlaybackPolicy, AuthenticatedPlansCommitOnlyAfterSubmissionIncludingZeroFrames) {
  BufferedPlaybackPolicy policy(0.25);
  policy.seedPlayerSequence(0x7fffff);
  auto first = policy.planAuthenticated(1000, true, 1024);
  EXPECT_TRUE(first.first);
  EXPECT_TRUE(first.mute);
  EXPECT_FALSE(first.skipTooOld);
  EXPECT_EQ(first.sequence, 0xffff);
  EXPECT_EQ(first.firstTimestamp, 1000u);
  EXPECT_EQ(policy.planAuthenticated(2000, true, 1024).sequence, 0xffff);
  policy.didSubmit(1000, 0);
  auto next = policy.planAuthenticated(1000, true, 1024);
  EXPECT_FALSE(next.first);
  EXPECT_FALSE(next.mute);
  EXPECT_EQ(next.sequence, 0);
  EXPECT_EQ(next.gap, 0);
  EXPECT_EQ(next.expectedTimestamp, 1000u);
}
TEST(BufferedPlaybackPolicy, SignedGapBoundaryMutesAacAndSkipsOnlyMoreThanOneNegativeBlock) {
  BufferedPlaybackPolicy policy(0.25);
  policy.didSubmit(1000, 1024);
  EXPECT_TRUE(policy.planAuthenticated(3048, true, 1024).mute);
  EXPECT_FALSE(policy.planAuthenticated(3048, false, 352).mute);
  auto boundary = policy.planAuthenticated(1000, true, 1024);
  EXPECT_EQ(boundary.gap, -1024);
  EXPECT_FALSE(boundary.skipTooOld);
  auto old = policy.planAuthenticated(999, true, 1024);
  EXPECT_EQ(old.gap, -1025);
  EXPECT_TRUE(old.skipTooOld);
  EXPECT_EQ(policy.planAuthenticated(2024, true, 1024).sequence, 1);
}
TEST(BufferedPlaybackPolicy, StopRetainsSequenceExpectedTimestampAndWarningButResetsFirstPacket) {
  BufferedPlaybackPolicy policy(0.25);
  policy.onPlayState(true);
  policy.seedPlayerSequence(42);
  policy.planAuthenticated(1000, true, 1024);
  policy.didSubmit(1000, 1024);
  EXPECT_TRUE(policy.admit(1450000001, 1000000000, 1024, 48000).warnEarly);
  policy.onPlayState(false);
  policy.onPlayState(true);
  EXPECT_FALSE(policy.admit(1450000001, 1000000000, 1024, 48000).warnEarly);
  auto resumed = policy.planAuthenticated(3000, true, 1024);
  EXPECT_TRUE(resumed.first);
  EXPECT_TRUE(resumed.mute);
  EXPECT_EQ(resumed.gap, 0);
  EXPECT_EQ(resumed.sequence, 43);
  EXPECT_EQ(resumed.expectedTimestamp, 2024u);
  EXPECT_EQ(resumed.firstTimestamp, 3000u);
}
TEST(BufferedPlaybackPolicy, TimestampWrapAndConstantPreviousScheduleRemainCompatible) {
  BufferedPlaybackPolicy policy(0.25);
  policy.onPlayState(true);
  policy.planAuthenticated(0xfffffff0, false, 352);
  policy.didSubmit(0xfffffff0, 352);
  EXPECT_EQ(policy.planAuthenticated(336, false, 352).gap, 0);
  EXPECT_EQ(policy.admit(0, 0, 352, 44100).kind, BufferedAdmissionKind::dropBeforePrevious);
  EXPECT_EQ(policy.admit(1, 1, 352, 44100).kind, BufferedAdmissionKind::prepare);
}
TEST(BufferedPlaybackPolicy, MinimumSignedTimestampGapIsTooOldWithoutSignedNegationOverflow) {
  BufferedPlaybackPolicy policy(0.25);
  policy.didSubmit(0, 0);
  auto plan = policy.planAuthenticated(0x80000000, true, 1024);
  EXPECT_EQ(plan.gap, INT32_MIN);
  EXPECT_TRUE(plan.skipTooOld);
  EXPECT_EQ(policy.planAuthenticated(0, true, 1024).sequence, 1);
}
