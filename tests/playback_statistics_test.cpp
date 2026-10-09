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
}
