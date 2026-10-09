#include "playback_sync.hpp"
#include <gtest/gtest.h>

static void checkInitialFrameError(PlaybackSync &sync, SyncObservation &observation) {
  observation.expectedFrameTime = 999000000;
  observation.dacMeasurementTime = 1000000000;
  observation.measurementDuration = 0;
  observation.measured = true;
  observation.timestamp = 1000;
  observation.firstFrame = true;
  observation.inputRate = observation.outputRate = 44100;
  observation.blockFrames = 352;
  observation.playNumber = 1;
  auto initial = sync.observe(observation, {false, 1000000, 0});
  EXPECT_EQ(initial.errorFrames, 44);
  EXPECT_EQ(initial.correctionFrames, 0);
  EXPECT_EQ(sync.skipFrom(352).frames, 44);
}

static void checkRetainedFrames(PlaybackSync &sync, SyncObservation &observation) {
  checkInitialFrameError(sync, observation);
  sync.resetForPlay();
  observation.firstFrame = false;
  observation.inputRate = observation.outputRate = 1000;
  observation.expectedFrameTime = observation.dacMeasurementTime;
  observation.retainedFrames = 17;
  observation.measurementDuration = 2000000;
  EXPECT_EQ(sync.observe(observation, {true, 0, 0}).errorFrames, 0);
  observation.retainedFrames = 0;
  observation.measurementDuration = 0;
  auto retained = sync.observe(observation, {true, 0, 0});
  EXPECT_EQ(retained.errorFrames, 17);
  EXPECT_EQ(retained.errorNs, 17000000);
  EXPECT_EQ(sync.observe(observation, {true, 0, 0}).errorFrames, 0);
}

static void checkUnavailableMeasurements(PlaybackSync &sync, SyncObservation &observation) {
  checkRetainedFrames(sync, observation);
  sync.resetForPlay();
  observation.measured = false;
  observation.retainedFrames = 17;
  EXPECT_EQ(sync.observe(observation, {true, 0, 0}).correctionFrames, 0);
  observation.measured = true;
  observation.retainedFrames = 0;
  EXPECT_EQ(sync.observe(observation, {true, 0, 0}).errorFrames, 0);
  observation.outputRate = 0;
  EXPECT_EQ(sync.observe(observation, {true, 0, 0}).correctionFrames, 0);
}

static void checkInitialSilenceAndPrefixSkipping(PlaybackSync &sync, SyncObservation &observation) {
  checkUnavailableMeasurements(sync, observation);
  sync.resetForPlay();
  observation.outputRate = observation.inputRate = 1000;
  observation.firstFrame = true;
  observation.expectedFrameTime = observation.dacMeasurementTime + 5000000;
  auto early = sync.observe(observation, {false, 0, 0});
  EXPECT_EQ(early.silenceFrames, 5);
  EXPECT_EQ(early.errorFrames, 0);
  observation.expectedFrameTime = observation.dacMeasurementTime - 100000000;
  auto late = sync.observe(observation, {false, 0, 0});
  EXPECT_EQ(late.errorFrames, 100);
  EXPECT_EQ(sync.skipFrom(30).frames, 30);
  EXPECT_EQ(sync.skipFrom(30).frames, 30);
  EXPECT_EQ(sync.skipFrom(100).frames, 40);
  EXPECT_EQ(sync.skipFrom(100).frames, 0);
}

static void checkTimestampGapRateConversion(PlaybackSync &sync, SyncObservation &observation) {
  checkInitialSilenceAndPrefixSkipping(sync, observation);
  sync.resetForPlay();
  observation.firstFrame = false;
  observation.expectedFrameTime = observation.dacMeasurementTime;
  observation.timestampGap = -352;
  observation.inputRate = 44100;
  observation.outputRate = 48000;
  sync.observe(observation, {false, 0, 0});
  EXPECT_EQ(sync.skipFrom(200).frames, 200);
  EXPECT_EQ(sync.skipFrom(200).frames, 183);
}

static void checkSubsequentPrefixSkipping(PlaybackSync &sync, SyncObservation &observation) {
  checkTimestampGapRateConversion(sync, observation);
  sync.resetForPlay();
  observation.timestampGap = 0;
  observation.inputRate = observation.outputRate = 1000;
  observation.firstFrame = true;
  sync.observe(observation, {false, 0, 0});
  EXPECT_EQ(sync.skipFrom(352).frames, 0);
  observation.firstFrame = false;
  observation.expectedFrameTime -= 1000000;
  sync.observe(observation, {false, 0, 0});
  EXPECT_EQ(sync.skipFrom(352).frames, 1);
}

static void checkCorrectionWarmup(PlaybackSync &sync, SyncObservation &observation) {
  checkSubsequentPrefixSkipping(sync, observation);
  sync.resetForPlay();
  observation.firstFrame = false;
  observation.blockFrames = 512;
  observation.playNumber = 19;
  observation.expectedFrameTime = observation.dacMeasurementTime - 2000000;
  EXPECT_EQ(sync.observe(observation, {true, 1000000, 0}).correctionFrames, 0);
  observation.playNumber = 20;
  EXPECT_EQ(sync.observe(observation, {true, 1000000, 0}).correctionFrames, -1);
  EXPECT_EQ(sync.observe(observation, {false, 1000000, 1e-9}).correctionFrames, 0);
}

static void checkResyncWindowBoundaries(PlaybackSync &sync, SyncObservation &observation) {
  checkCorrectionWarmup(sync, observation);
  for (int sign : {-1, 1}) {
    sync.resetForPlay();
    observation.expectedFrameTime = observation.dacMeasurementTime - sign * int64_t{2000000};
    for (unsigned index = 0; index < 39; ++index)
      EXPECT_FALSE(sync.observe(observation, {true, 2000000, 0.001999999}).dropPacket);
    auto exact = sync.observe(observation, {true, 2000000, 0.002});
    EXPECT_EQ(exact.correctionFrames, 0);
    EXPECT_FALSE(exact.dropPacket);
    EXPECT_EQ(exact.windowSpreadNs, 0);
    auto exceeded = sync.observe(observation, {true, 1999999, 0.001999999});
    EXPECT_TRUE(exceeded.dropPacket);
    EXPECT_EQ(exceeded.resyncNext, sign < 0);
    EXPECT_EQ(exceeded.correctionFrames, -sign);
  }
}

static void checkWindowSpreadAndTolerance(PlaybackSync &sync, SyncObservation &observation) {
  checkResyncWindowBoundaries(sync, observation);
  sync.resetForPlay();
  for (int64_t error : {4000000, 2000000, 6000000, 3000000}) {
    observation.expectedFrameTime = observation.dacMeasurementTime - error;
    auto current = sync.observe(observation, {true, 3500000, 0});
    if (error == 3000000) {
      EXPECT_EQ(current.windowSpreadNs, 1000000);
      EXPECT_EQ(current.correctionFrames, 0);
    }
  }
  EXPECT_EQ(sync.observe(observation, {true, 3499999, 0}).correctionFrames, -1);
}

static void checkFirstFrameAndRateChangeHistory(PlaybackSync &sync, SyncObservation &observation) {
  checkWindowSpreadAndTolerance(sync, observation);
  sync.resetForPlay();
  observation.expectedFrameTime = observation.dacMeasurementTime - 2000000;
  for (unsigned index = 0; index < 40; ++index)
    sync.observe(observation, {true, 1000000, 0});
  observation.firstFrame = true;
  sync.observe(observation, {true, 1000000, 0.001});
  EXPECT_EQ(sync.skipFrom(352).frames, 2);
  observation.firstFrame = false;
  observation.inputRate = observation.outputRate = 2000;
  EXPECT_TRUE(sync.observe(observation, {true, 1000000, 0.001}).dropPacket);
  observation.expectedFrameTime = observation.dacMeasurementTime + 2000000;
  EXPECT_FALSE(sync.observe(observation, {true, 1000000, 0.001}).dropPacket);
  sync.resetForPlay();
  EXPECT_FALSE(sync.observe(observation, {true, 1000000, 0.001}).dropPacket);
}

TEST(PlaybackSync, InitialFrameErrorCreatesPrefixSkipWithoutCorrection) {
  PlaybackSync sync;
  SyncObservation observation;
  checkInitialFrameError(sync, observation);
}

TEST(PlaybackSync, RetainedFramesAffectOnlyFollowingMeasurement) {
  PlaybackSync sync;
  SyncObservation observation;
  checkRetainedFrames(sync, observation);
}

TEST(PlaybackSync, UnmeasuredAndZeroRateObservationsDoNotCorrect) {
  PlaybackSync sync;
  SyncObservation observation;
  checkUnavailableMeasurements(sync, observation);
}

TEST(PlaybackSync, InitialEarlyFramesCreateSilenceAndLateFramesConsumePrefix) {
  PlaybackSync sync;
  SyncObservation observation;
  checkInitialSilenceAndPrefixSkipping(sync, observation);
}

TEST(PlaybackSync, TimestampGapConvertsBetweenInputAndOutputRates) {
  PlaybackSync sync;
  SyncObservation observation;
  checkTimestampGapRateConversion(sync, observation);
}

TEST(PlaybackSync, SubsequentFrameErrorAddsPrefixSkip) {
  PlaybackSync sync;
  SyncObservation observation;
  checkSubsequentPrefixSkipping(sync, observation);
}

TEST(PlaybackSync, CorrectionsBeginAtPlayTwentyOnlyWhenSyncIsEnabled) {
  PlaybackSync sync;
  SyncObservation observation;
  checkCorrectionWarmup(sync, observation);
}

TEST(PlaybackSync, BothErrorSignsRequireFortySamplesAndStrictlyExceededResyncThreshold) {
  PlaybackSync sync;
  SyncObservation observation;
  checkResyncWindowBoundaries(sync, observation);
}

TEST(PlaybackSync, WindowSpreadAndCorrectionUseStrictToleranceBoundary) {
  PlaybackSync sync;
  SyncObservation observation;
  checkWindowSpreadAndTolerance(sync, observation);
}

TEST(PlaybackSync, FirstFrameAndRateChangesPreserveHistoryUntilPlayReset) {
  PlaybackSync sync;
  SyncObservation observation;
  checkFirstFrameAndRateChangeHistory(sync, observation);
}

TEST(PlaybackSync, NanosecondWraparoundPreservesFrameError) {
  PlaybackSync sync;
  SyncObservation observation;
  checkFirstFrameAndRateChangeHistory(sync, observation);
  sync.resetForPlay();
  observation.firstFrame = false;
  observation.inputRate = observation.outputRate = 1000;
  observation.expectedFrameTime = UINT64_MAX - 499999;
  observation.dacMeasurementTime = 500000;
  auto wrapped = sync.observe(observation, {true, 1000000, 0});
  EXPECT_EQ(wrapped.errorNs, 1000000);
  EXPECT_EQ(wrapped.errorFrames, 1);
}
