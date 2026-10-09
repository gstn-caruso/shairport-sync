#include "playback_statistics.hpp"
#include <cassert>

int main() {
  PlaybackStatistics statistics;
  assert(!statistics.hasArrivals());
  statistics.recordArrival(100, 10, ArrivalKind::tooLate);
  auto first = statistics.snapshot();
  assert(first.packets == 1 && first.tooLate == 1 && first.inputFramesPerSecond == 0);
  for (unsigned count = 2; count <= 499; ++count)
    statistics.recordArrival(count * 1000000ULL, count, ArrivalKind::inOrder);
  assert(!statistics.snapshot().inputRateAvailable);
  statistics.recordArrival(500000000, 0xfffffff0U, ArrivalKind::inOrder);
  statistics.recordArrival(501000000, 28, ArrivalKind::inOrder);
  auto measured = statistics.snapshot();
  assert(measured.inputRateAvailable && measured.inputFramesPerSecond == 44000);
  statistics.resetInputEpoch();
  assert(statistics.hasArrivals());
  assert(!statistics.snapshot().inputRateAvailable);
  assert(statistics.snapshot().packets == 501);
  auto attempt = statistics.recordPlaybackAttempt(1000000000);
  assert(attempt.playNumber == 1 && statistics.hasPlaybackSinceFlush());
  assert(statistics.observeFrame(7));
  statistics.recordSync({.windowSpreadNs = 2000000});
  statistics.recordSubmitted(8000, 10, -1);
  assert(!statistics.takeIntervalIfDue(1000));
  statistics.recordSubmitted(1, -2, 1);
  auto interval = statistics.takeIntervalIfDue(1000);
  assert(interval && interval->frames == 8001 && interval->measurements == 2);
  assert(interval->packets == 501 && interval->playNumber == 1 && interval->hasObservedFrame);
  assert(interval->minimumBufferOccupancy == 7 && interval->corrections == 0);
  assert(!statistics.takeIntervalIfDue(1000));
  statistics.resetInputEpoch();
  assert(!statistics.hasPlaybackSinceFlush());
  assert(!statistics.observeFrame(9));
  statistics.recordPlaybackAttempt(2000000000);
  assert(statistics.sessionSummary(3000000000).elapsedSeconds == 2);
}
