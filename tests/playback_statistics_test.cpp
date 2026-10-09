#include "playback/playback_statistics.hpp"
#include <gtest/gtest.h>
#include <thread>

static void checkInputRateAcrossFrameWraparound(PlaybackStatistics &statistics) {
  EXPECT_FALSE(statistics.hasArrivals());
  statistics.recordArrival(100, 10, ArrivalKind::tooLate);
  auto first = statistics.snapshot();
  EXPECT_EQ(first.packets, 1);
  EXPECT_EQ(first.tooLate, 1);
  EXPECT_EQ(first.inputFramesPerSecond, 0);
  for (unsigned count = 2; count <= 499; ++count)
    statistics.recordArrival(count * 1000000ULL, count, ArrivalKind::inOrder);
  EXPECT_FALSE(statistics.snapshot().inputRateAvailable);
  statistics.recordArrival(500000000, 0xfffffff0U, ArrivalKind::inOrder);
  statistics.recordArrival(501000000, 28, ArrivalKind::inOrder);
  auto measured = statistics.snapshot();
  EXPECT_TRUE(measured.inputRateAvailable);
  EXPECT_EQ(measured.inputFramesPerSecond, 44000);
}

static void checkPlaybackInterval(PlaybackStatistics &statistics) {
  checkInputRateAcrossFrameWraparound(statistics);
  statistics.resetInputEpoch();
  EXPECT_TRUE(statistics.hasArrivals());
  EXPECT_FALSE(statistics.snapshot().inputRateAvailable);
  EXPECT_EQ(statistics.snapshot().packets, 501);
  auto attempt = statistics.recordPlaybackAttempt(1000000000);
  EXPECT_EQ(attempt.playNumber, 1);
  EXPECT_TRUE(statistics.hasPlaybackSinceFlush());
  EXPECT_TRUE(statistics.observeFrame(7));
  statistics.recordSync({.windowSpreadNs = 2000000});
  statistics.recordSubmitted(8000, 10, -1);
  EXPECT_FALSE(statistics.takeIntervalIfDue(1000));
  statistics.recordSubmitted(1, -2, 1);
  auto interval = statistics.takeIntervalIfDue(1000);
  ASSERT_TRUE(interval);
  EXPECT_EQ(interval->frames, 8001);
  EXPECT_EQ(interval->measurements, 2);
  EXPECT_EQ(interval->packets, 501);
  EXPECT_EQ(interval->playNumber, 1);
  EXPECT_TRUE(interval->hasObservedFrame);
  EXPECT_EQ(interval->minimumBufferOccupancy, 7);
  EXPECT_EQ(interval->corrections, 0);
  EXPECT_FALSE(statistics.takeIntervalIfDue(1000));
}

static void checkSessionAndOutputRates(PlaybackStatistics &statistics) {
  ASSERT_NO_FATAL_FAILURE(checkPlaybackInterval(statistics));
  statistics.resetInputEpoch();
  EXPECT_FALSE(statistics.hasPlaybackSinceFlush());
  EXPECT_FALSE(statistics.observeFrame(9));
  statistics.recordPlaybackAttempt(2000000000);
  EXPECT_EQ(statistics.sessionSummary(3000000000).elapsedSeconds, 2);
  statistics.recordOutputReading({0, 1000000000, 2000000000, 10, 100});
  EXPECT_FALSE(statistics.snapshot().outputRateAvailable);
  statistics.recordOutputReading({0, 2000000000, 3000000000, 10, 44100 + 100});
  auto output = statistics.snapshot();
  EXPECT_TRUE(output.outputRateAvailable);
  EXPECT_EQ(output.rawOutputFramesPerSecond, 44100);
  statistics.recordOutputReading({0, 2000000000, 2000000000, 10, 44200});
  EXPECT_FALSE(statistics.snapshot().outputRateAvailable);
  statistics.recordOutputReading({1, 3000000000, 4000000000, 10, 90000});
  EXPECT_FALSE(statistics.snapshot().outputRateAvailable);
}

static void checkConcurrentCountersAndMinima(PlaybackStatistics &statistics) {
  ASSERT_NO_FATAL_FAILURE(checkSessionAndOutputRates(statistics));
  statistics.observeBufferedBytes(20000);
  statistics.observeBufferedBytes(12000);
  statistics.recordDacQueue(17);
  std::thread producerA([&] { for (unsigned i = 0; i < 1000; ++i) statistics.recordMissingPlayback(); });
  std::thread producerB([&] { for (unsigned i = 0; i < 1000; ++i) statistics.recordResendRequested(); });
  producerA.join();
  producerB.join();
  auto concurrent = statistics.snapshot();
  EXPECT_EQ(concurrent.missing, 1000);
  EXPECT_EQ(concurrent.resends, 1000);
  EXPECT_EQ(concurrent.minimumBufferedBytes, 12000);
  EXPECT_EQ(concurrent.minimumDacQueue, 17);
}

static void checkZeroFrameSubmissionAndPlayReset(PlaybackStatistics &statistics) {
  ASSERT_NO_FATAL_FAILURE(checkConcurrentCountersAndMinima(statistics));
  statistics.recordPlaybackAttempt(3000000000);
  statistics.recordSubmitted(0, 99, 20);
  EXPECT_EQ(statistics.snapshot().measurements, 0);
  statistics.recordArrival(0, 0, ArrivalKind::late);
  EXPECT_EQ(statistics.snapshot().late, 1);
  statistics.resetForPlay();
  EXPECT_FALSE(statistics.hasArrivals());
  EXPECT_FALSE(statistics.hasPlaybackSinceFlush());
  EXPECT_FALSE(statistics.sessionSummary(0).hasObservedFrame);
}

static void checkInputRateAfterAheadArrivals(PlaybackStatistics &statistics) {
  ASSERT_NO_FATAL_FAILURE(checkZeroFrameSubmissionAndPlayReset(statistics));
  for (unsigned i = 1; i < 510; ++i)
    statistics.recordArrival(i, i, ArrivalKind::ahead);
  statistics.recordArrival(510, 10, ArrivalKind::inOrder);
  EXPECT_FALSE(statistics.snapshot().inputRateAvailable);
  statistics.recordArrival(1000000510, 44110, ArrivalKind::inOrder);
  EXPECT_EQ(statistics.snapshot().inputFramesPerSecond, 44100);
}

TEST(PlaybackStatistics, InputRateWarmupAndFrameWraparoundPreservePacketCounts) {
  PlaybackStatistics statistics;
  checkInputRateAcrossFrameWraparound(statistics);
}

TEST(PlaybackStatistics, EpochResetRetainsArrivalsAndPlaybackIntervalIsConsumedOnce) {
  PlaybackStatistics statistics;
  checkPlaybackInterval(statistics);
}

TEST(PlaybackStatistics, SessionElapsedTimeAndOutputRateValidityFollowReadingHistory) {
  PlaybackStatistics statistics;
  checkSessionAndOutputRates(statistics);
}

TEST(PlaybackStatistics, ConcurrentProducersPreserveCountersAndBufferMinima) {
  PlaybackStatistics statistics;
  checkConcurrentCountersAndMinima(statistics);
}

TEST(PlaybackStatistics, ZeroFrameSubmissionAndPlayResetClearUsedState) {
  PlaybackStatistics statistics;
  checkZeroFrameSubmissionAndPlayReset(statistics);
}

TEST(PlaybackStatistics, AheadArrivalsAllowRateMeasurementAfterInOrderAnchor) {
  PlaybackStatistics statistics;
  checkInputRateAfterAheadArrivals(statistics);
}

TEST(PlaybackStatistics, TooLateArrivalsDoNotWarmUpRateAfterReset) {
  PlaybackStatistics statistics;
  ASSERT_NO_FATAL_FAILURE(checkInputRateAfterAheadArrivals(statistics));
  statistics.resetForPlay();
  for (unsigned i = 1; i <= 510; ++i)
    statistics.recordArrival(i, i, ArrivalKind::tooLate);
  statistics.recordArrival(511, 511, ArrivalKind::inOrder);
  statistics.recordArrival(512, 512, ArrivalKind::inOrder);
  EXPECT_FALSE(statistics.snapshot().inputRateAvailable);
}
