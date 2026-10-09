#include "playback_statistics.hpp"
#include <cassert>
#include <thread>

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
  statistics.recordOutputReading({0, 1000000000, 2000000000, 10, 100});
  assert(!statistics.snapshot().outputRateAvailable);
  statistics.recordOutputReading({0, 2000000000, 3000000000, 10, 44100 + 100});
  auto output = statistics.snapshot();
  assert(output.outputRateAvailable && output.rawOutputFramesPerSecond == 44100);
  statistics.recordOutputReading({0, 2000000000, 2000000000, 10, 44200});
  assert(!statistics.snapshot().outputRateAvailable);
  statistics.recordOutputReading({1, 3000000000, 4000000000, 10, 90000});
  assert(!statistics.snapshot().outputRateAvailable);
  statistics.observeBufferedBytes(20000);
  statistics.observeBufferedBytes(12000);
  statistics.recordDacQueue(17);
  std::thread producerA([&] { for (unsigned i = 0; i < 1000; ++i) statistics.recordMissingPlayback(); });
  std::thread producerB([&] { for (unsigned i = 0; i < 1000; ++i) statistics.recordResendRequested(); });
  producerA.join();
  producerB.join();
  auto concurrent = statistics.snapshot();
  assert(concurrent.missing == 1000 && concurrent.resends == 1000);
  assert(concurrent.minimumBufferedBytes == 12000 && concurrent.minimumDacQueue == 17);
  statistics.recordPlaybackAttempt(3000000000);
  statistics.recordSubmitted(0, 99, 20);
  assert(statistics.snapshot().measurements == 0);
  statistics.recordArrival(0, 0, ArrivalKind::late);
  assert(statistics.snapshot().late == 1);
  statistics.resetForPlay();
  assert(!statistics.hasArrivals() && !statistics.hasPlaybackSinceFlush());
  assert(!statistics.sessionSummary(0).hasObservedFrame);
  for (unsigned i = 1; i < 510; ++i)
    statistics.recordArrival(i, i, ArrivalKind::ahead);
  statistics.recordArrival(510, 10, ArrivalKind::inOrder);
  assert(!statistics.snapshot().inputRateAvailable);
  statistics.recordArrival(1000000510, 44110, ArrivalKind::inOrder);
  assert(statistics.snapshot().inputFramesPerSecond == 44100);
  statistics.resetForPlay();
  for (unsigned i = 1; i <= 510; ++i)
    statistics.recordArrival(i, i, ArrivalKind::tooLate);
  statistics.recordArrival(511, 511, ArrivalKind::inOrder);
  statistics.recordArrival(512, 512, ArrivalKind::inOrder);
  assert(!statistics.snapshot().inputRateAvailable);
}
