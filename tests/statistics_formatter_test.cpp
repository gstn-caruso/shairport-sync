#include "playback/statistics_formatter.hpp"
#include <gtest/gtest.h>

static void checkHeaders(StatisticsStream stream, bool delay, bool debugging,
                         const std::string &expected) {
  for (bool backend : {false, true}) {
    StatisticsFormatter formatter({stream, delay, backend, debugging});
    EXPECT_EQ(formatter.header(), expected + (delay && backend ? " | Output FPS (r) | Output FPS (c)" : ""))
        << "backend statistics: " << backend;
  }
}

TEST(StatisticsFormatter, BufferedHeaderWithoutDelayOrDebuggingIsEmpty) {
  checkHeaders(StatisticsStream::buffered, false, false, "");
}

TEST(StatisticsFormatter, BufferedDebugHeaderWithoutDelayShowsBufferSize) {
  checkHeaders(StatisticsStream::buffered, false, true, "Min Buffer Size");
}

TEST(StatisticsFormatter, BufferedHeaderWithDelayShowsSyncAndAvailableOutputRates) {
  checkHeaders(StatisticsStream::buffered, true, false,
               "Av Sync Error (ms) | Net Sync PPM | All Sync PPM");
}

TEST(StatisticsFormatter, BufferedDebugHeaderWithDelayShowsSyncAndBufferMetrics) {
  checkHeaders(StatisticsStream::buffered, true, true,
               "Av Sync Error (ms) | Net Sync PPM | All Sync PPM | Av Sync Window (ms) | Min DAC Queue | Min Buffers | Min Buffer Size");
}

TEST(StatisticsFormatter, RealtimeHeaderWithoutDelayShowsMissingPacketsAndResends) {
  checkHeaders(StatisticsStream::realtime, false, false, "Missing | Resend Reqs");
}

TEST(StatisticsFormatter, RealtimeDebugHeaderWithoutDelayShowsPacketAndBufferMetrics) {
  checkHeaders(StatisticsStream::realtime, false, true,
               "Missing |   Late | Too Late | Resend Reqs | Min Buffers | Max Buffers | Received FPS");
}

TEST(StatisticsFormatter, RealtimeHeaderWithDelayShowsSyncMissingPacketsAndResends) {
  checkHeaders(StatisticsStream::realtime, true, false,
               "Av Sync Error (ms) | All Sync PPM | Av Sync Window (ms) | Missing | Resend Reqs");
}

TEST(StatisticsFormatter, RealtimeDebugHeaderWithDelayShowsAllSyncAndPacketMetrics) {
  checkHeaders(StatisticsStream::realtime, true, true,
               "Av Sync Error (ms) | Net Sync PPM | All Sync PPM | Av Sync Window (ms) | Missing |   Late | Too Late | Resend Reqs | Min DAC Queue | Min Buffers | Max Buffers | Received FPS");
}

TEST(StatisticsFormatter, BufferedRowWithoutMeasurementsShowsInitialMinimumBytes) {
  PlaybackStatisticsSnapshot snapshot;
  StatisticsFormatter buffered({StatisticsStream::buffered, false, false, true});
  EXPECT_EQ(buffered.row(snapshot), "     4294967295");
}

TEST(StatisticsFormatter, BufferedRowFormatsMinimumBytesInKilobytes) {
  PlaybackStatisticsSnapshot snapshot;
  snapshot.minimumBufferedBytes = 12000;
  StatisticsFormatter buffered({StatisticsStream::buffered, false, false, true});
  EXPECT_EQ(buffered.row(snapshot), "            11k");
}

TEST(StatisticsFormatter, RealtimeRowFormatsMissingPacketAndResendCounts) {
  PlaybackStatisticsSnapshot snapshot;
  snapshot.minimumBufferedBytes = 12000;
  snapshot.missing = 42;
  snapshot.resends = 3;
  StatisticsFormatter realtime({StatisticsStream::realtime, false, false, false});
  EXPECT_EQ(realtime.row(snapshot), "     42             3");
}

TEST(StatisticsFormatter, SyncedRowShowsUnavailableOutputRates) {
  PlaybackStatisticsSnapshot snapshot;
  snapshot.minimumBufferedBytes = 12000;
  snapshot.missing = 42;
  snapshot.resends = 3;
  StatisticsFormatter synced({StatisticsStream::buffered, true, true, false});
  EXPECT_EQ(synced.row(snapshot), "              0.00            0.0            0.0              N/A              N/A");
}

TEST(StatisticsFormatter, SessionWithoutOutputRateShowsZeroElapsedTime) {
  PlaybackSessionSummary summary;
  StatisticsFormatter synced({StatisticsStream::buffered, true, true, false});
  EXPECT_EQ(synced.session(7, summary), "Connection 7: Playback stopped. Total playing time 00:00:00.");
}

TEST(StatisticsFormatter, SessionFormatsElapsedTimeAndRawCorrectedOutputRates) {
  PlaybackSessionSummary summary;
  summary.elapsedSeconds = 3661;
  summary.outputRateAvailable = true;
  summary.rawOutputFramesPerSecond = 44099.25;
  summary.correctedOutputFramesPerSecond = 44100.5;
  StatisticsFormatter synced({StatisticsStream::buffered, true, true, false});
  EXPECT_EQ(synced.session(7, summary), "Connection 7: Playback stopped. Total playing time 01:01:01. Output: 44099.25 (raw), 44100.50 (corrected) frames per second.");
}
