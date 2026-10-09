#include "statistics_formatter.hpp"
#include <cassert>

int main() {
  for (auto stream : {StatisticsStream::realtime, StatisticsStream::buffered})
    for (bool delay : {false, true})
      for (bool backend : {false, true})
        for (bool debugging : {false, true}) {
          StatisticsFormatter formatter({stream, delay, backend, debugging});
          std::string expected;
          if (stream == StatisticsStream::buffered) {
            if (delay)
              expected = debugging ? "Av Sync Error (ms) | Net Sync PPM | All Sync PPM | Av Sync Window (ms) | Min DAC Queue | Min Buffers | Min Buffer Size" :
                                     "Av Sync Error (ms) | Net Sync PPM | All Sync PPM";
            else if (debugging) expected = "Min Buffer Size";
          } else if (delay) {
            expected = debugging ? "Av Sync Error (ms) | Net Sync PPM | All Sync PPM | Av Sync Window (ms) | Missing |   Late | Too Late | Resend Reqs | Min DAC Queue | Min Buffers | Max Buffers | Received FPS" :
                                   "Av Sync Error (ms) | All Sync PPM | Av Sync Window (ms) | Missing | Resend Reqs";
          } else {
            expected = debugging ? "Missing |   Late | Too Late | Resend Reqs | Min Buffers | Max Buffers | Received FPS" :
                                   "Missing | Resend Reqs";
          }
          if (delay && backend) expected += " | Output FPS (r) | Output FPS (c)";
          assert(formatter.header() == expected);
        }
  PlaybackStatisticsSnapshot snapshot;
  StatisticsFormatter buffered({StatisticsStream::buffered, false, false, true});
  assert(buffered.row(snapshot) == "     4294967295");
  snapshot.minimumBufferedBytes = 12000;
  assert(buffered.row(snapshot) == "            11k");
  snapshot.missing = 42;
  snapshot.resends = 3;
  StatisticsFormatter realtime({StatisticsStream::realtime, false, false, false});
  assert(realtime.row(snapshot) == "     42             3");
  StatisticsFormatter synced({StatisticsStream::buffered, true, true, false});
  assert(synced.row(snapshot) == "              0.00            0.0            0.0              N/A              N/A");
  PlaybackSessionSummary summary;
  assert(synced.session(7, summary) == "Connection 7: Playback stopped. Total playing time 00:00:00.");
  summary.elapsedSeconds = 3661;
  summary.outputRateAvailable = true;
  summary.rawOutputFramesPerSecond = 44099.25;
  summary.correctedOutputFramesPerSecond = 44100.5;
  assert(synced.session(7, summary) == "Connection 7: Playback stopped. Total playing time 01:01:01. Output: 44099.25 (raw), 44100.50 (corrected) frames per second.");
}
