#include "playback/timing/playback_timing.hpp"
#include <gtest/gtest.h>
#include <thread>

static void checkInitialPrerollAndRelease(PlaybackTiming &timing) {
  auto first = timing.startWithReadyPacket(1000);
  ASSERT_TRUE(first);
  EXPECT_TRUE(first->configureOutput);
  auto noTime = timing.planPreroll(*first, {1000000000, {}, false}, {44100, true, 0});
  EXPECT_EQ(noTime.silenceFrames, 0);
  EXPECT_FALSE(noTime.queryDelay);
  auto silence = timing.planPreroll(*first, {1000000000, 1150000000, false}, {44100, true, 0});
  EXPECT_EQ(silence.silenceFrames, 6615);
  EXPECT_EQ(silence.maximumChunkFrames, 4410);
  auto target = timing.releaseTargetFrame(1000, 100);
  EXPECT_EQ(target.frame, 900);
  EXPECT_FALSE(timing.shouldRelease(target, {1000000000, 1150000000, 0, -100}));
  EXPECT_TRUE(timing.shouldRelease(target, {1150000000, 1150000000, 0, -100}));
  EXPECT_TRUE(timing.isFirstFrame(1000));
}

static void checkPrimedDelayAndWrappedRelease(PlaybackTiming &timing,
                                             PrerollAction &negativeDelay,
                                             PlaybackTiming::ReleaseTarget &subsequent) {
  checkInitialPrerollAndRelease(timing);
  EXPECT_FALSE(timing.startWithReadyPacket(0xfffffff0));
  timing.resetForPlay();
  auto first = timing.startWithReadyPacket(0xfffffff0);
  ASSERT_TRUE(first);
  auto initial = timing.planPreroll(*first, {1000000000, 1200000000, true}, {48000, true, 0});
  EXPECT_EQ(initial.silenceFrames, 4800);
  EXPECT_FALSE(initial.queryDelay);
  timing.markSilenceSubmitted(initial);
  auto query = timing.planPreroll(*first, {1000000000, 1200000000, true}, {48000, true, 0});
  EXPECT_TRUE(query.queryDelay);
  auto unavailable = timing.planPreroll(*first, {1000000000, 1200000000, true, true, -1, 100}, {48000, true, 0});
  EXPECT_EQ(unavailable.silenceFrames, 0);
  negativeDelay = timing.planPreroll(*first, {1000000000, 1050000000, true, true, 0, -100}, {48000, true, 0});
  EXPECT_EQ(negativeDelay.silenceFrames, 2500);
  auto target = timing.releaseTargetFrame(0xfffffff0, 4410);
  EXPECT_EQ(target.frame, uint32_t(0xfffffff0U - 4410));
  EXPECT_FALSE(timing.shouldRelease(target, {1000000000, 1000000001, 0, -100}));
  EXPECT_TRUE(timing.shouldRelease(target, {1000000001, 1000000001, 0, -100}));
  subsequent = timing.releaseTargetFrame(100, 4410);
  EXPECT_TRUE(timing.shouldRelease(subsequent, {1000000000, 2000000000, 0, -100}));
  EXPECT_FALSE(timing.shouldRelease(subsequent, {1000000000, 2000000000, 0, 4410}));
  EXPECT_FALSE(timing.shouldRelease(subsequent, {1000000000, 11000000000, -1, 0}));
  EXPECT_TRUE(timing.shouldRelease(subsequent, {1000000000, 11000000001, -1, 0}));
}

static void checkRestartHistory(PlaybackTiming &timing) {
  PrerollAction negativeDelay;
  PlaybackTiming::ReleaseTarget subsequent{};
  checkPrimedDelayAndWrappedRelease(timing, negativeDelay, subsequent);
  timing.onFlush();
  EXPECT_FALSE(timing.mayApply(negativeDelay));
  EXPECT_FALSE(timing.shouldRelease(subsequent, {20000000000, 1000000000, 0, 0}));
  auto first = timing.startWithReadyPacket(200);
  ASSERT_TRUE(first);
  auto unprimed = timing.planPreroll(*first, {1000000000, 1050000000, true}, {48000, true, 0});
  EXPECT_FALSE(unprimed.queryDelay);
  EXPECT_EQ(unprimed.silenceFrames, 2400);
  timing.onResync();
  first = timing.startWithReadyPacket(300);
  ASSERT_TRUE(first);
  EXPECT_TRUE(timing.planPreroll(*first, {1000000000, 1050000000, true}, {48000, true, 0}).queryDelay);
  timing.onArrival(ArrivalKind::overflow);
  EXPECT_FALSE(timing.mayApply(unprimed));
  first = timing.startWithReadyPacket(400);
  ASSERT_TRUE(first);
  EXPECT_TRUE(first->configureOutput);
  EXPECT_TRUE(timing.isFirstFrame(400));
  timing.onBufferReset();
  first = timing.startWithReadyPacket(400);
  ASSERT_TRUE(first);
  EXPECT_FALSE(first->configureOutput);
  timing.onArrival(ArrivalKind::first);
  first = timing.startWithReadyPacket(500);
  ASSERT_TRUE(first);
  EXPECT_TRUE(first->configureOutput);
}

static void checkConcurrentOverflowArrivals(PlaybackTiming &timing) {
  checkRestartHistory(timing);
  std::thread arrival([&] { for (unsigned i = 0; i < 1000; ++i) timing.onArrival(ArrivalKind::overflow); });
  for (unsigned i = 0; i < 1000; ++i) {
    auto start = timing.startWithReadyPacket(i + 1);
    EXPECT_TRUE(start);
    if (start)
      timing.planPreroll(*start, {1000000000, 1200000000, true}, {48000, false, 0});
  }
  arrival.join();
}

TEST(PlaybackTiming, InitialPrerollAndReleaseFollowFirstFrameTime) {
  PlaybackTiming timing;
  checkInitialPrerollAndRelease(timing);
}

TEST(PlaybackTiming, PrimedDelayAndWrappedReleaseRespectExactBoundaries) {
  PlaybackTiming timing;
  PrerollAction negativeDelay;
  PlaybackTiming::ReleaseTarget subsequent{};
  checkPrimedDelayAndWrappedRelease(timing, negativeDelay, subsequent);
}

TEST(PlaybackTiming, FlushResyncAndArrivalsInvalidatePlansAndControlOutputConfiguration) {
  PlaybackTiming timing;
  checkRestartHistory(timing);
}

TEST(PlaybackTiming, ConcurrentOverflowArrivalsAllowReadyPacketPlanning) {
  PlaybackTiming timing;
  checkConcurrentOverflowArrivals(timing);
}

TEST(PlaybackTiming, ManualLeadInBoundaryAndPrerollCompletionControlRelease) {
  PlaybackTiming timing;
  checkConcurrentOverflowArrivals(timing);
  auto beforeReset = timing.startWithReadyPacket(600);
  ASSERT_TRUE(beforeReset);
  EXPECT_TRUE(timing.planPreroll(*beforeReset, {1000000000, 1100000000, true}, {48000, true, 0}).queryDelay);
  timing.resetForPlay();
  auto first = timing.startWithReadyPacket(600);
  ASSERT_TRUE(first);
  EXPECT_GT(first->revision, beforeReset->revision);
  EXPECT_TRUE(first->configureOutput);
  auto held = timing.planPreroll(*first, {1000000000, 1100000001, true}, {48000, false, 100000000});
  EXPECT_EQ(held.silenceFrames, 0);
  auto exactLead = timing.planPreroll(*first, {1000000000, 1100000000, true}, {48000, false, 100000000});
  EXPECT_EQ(exactLead.silenceFrames, 4800);
  auto target = timing.releaseTargetFrame(600, 0);
  EXPECT_FALSE(timing.shouldRelease(target, {1100000000, 1100000000, 0, 0}));
  auto complete = timing.planPreroll(*first, {1100000000, 1100000000, true}, {48000, true, 0});
  EXPECT_EQ(complete.silenceFrames, 0);
  EXPECT_TRUE(timing.shouldRelease(target, {1100000000, 1100000000, 0, 0}));
}
