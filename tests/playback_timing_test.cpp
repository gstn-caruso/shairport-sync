#include "playback_timing.hpp"
#include <cassert>
#include <thread>

int main() {
  PlaybackTiming timing;
  auto first = timing.startWithReadyPacket(1000);
  assert(first && first->configureOutput);
  auto noTime = timing.planPreroll(*first, {1000000000, {}, false}, {44100, true, 0});
  assert(noTime.silenceFrames == 0 && !noTime.queryDelay);
  auto silence = timing.planPreroll(*first, {1000000000, 1150000000, false}, {44100, true, 0});
  assert(silence.silenceFrames == 6615 && silence.maximumChunkFrames == 4410);
  auto target = timing.releaseTargetFrame(1000, 100);
  assert(target.frame == 900);
  assert(!timing.shouldRelease(target, {1000000000, 1150000000, 0, -100}));
  assert(timing.shouldRelease(target, {1150000000, 1150000000, 0, -100}));
  assert(timing.isFirstFrame(1000));
  timing.resetForPlay();
  first = timing.startWithReadyPacket(0xfffffff0);
  auto initial = timing.planPreroll(*first, {1000000000, 1200000000, true}, {48000, true, 0});
  assert(initial.silenceFrames == 4800 && !initial.queryDelay);
  timing.markSilenceSubmitted(initial);
  auto query = timing.planPreroll(*first, {1000000000, 1200000000, true}, {48000, true, 0});
  assert(query.queryDelay);
  auto unavailable = timing.planPreroll(*first, {1000000000, 1200000000, true, true, -1, 100}, {48000, true, 0});
  assert(unavailable.silenceFrames == 0);
  auto negativeDelay = timing.planPreroll(*first, {1000000000, 1050000000, true, true, 0, -100}, {48000, true, 0});
  assert(negativeDelay.silenceFrames == 2500);
  target = timing.releaseTargetFrame(0xfffffff0, 4410);
  assert(target.frame == uint32_t(0xfffffff0U - 4410));
  assert(!timing.shouldRelease(target, {1000000000, 1000000001, 0, -100}));
  assert(timing.shouldRelease(target, {1000000001, 1000000001, 0, -100}));
  auto subsequent = timing.releaseTargetFrame(100, 4410);
  assert(timing.shouldRelease(subsequent, {1000000000, 2000000000, 0, -100}));
  assert(!timing.shouldRelease(subsequent, {1000000000, 2000000000, 0, 4410}));
  assert(!timing.shouldRelease(subsequent, {1000000000, 11000000000, -1, 0}));
  assert(timing.shouldRelease(subsequent, {1000000000, 11000000001, -1, 0}));
  timing.onFlush();
  assert(!timing.mayApply(negativeDelay));
  assert(!timing.shouldRelease(subsequent, {20000000000, 1000000000, 0, 0}));
  first = timing.startWithReadyPacket(200);
  auto unprimed = timing.planPreroll(*first, {1000000000, 1050000000, true}, {48000, true, 0});
  assert(!unprimed.queryDelay && unprimed.silenceFrames == 2400);
  timing.onResync();
  first = timing.startWithReadyPacket(300);
  assert(timing.planPreroll(*first, {1000000000, 1050000000, true}, {48000, true, 0}).queryDelay);
  timing.onArrival(ArrivalKind::overflow);
  assert(!timing.mayApply(unprimed));
  first = timing.startWithReadyPacket(400);
  assert(first->configureOutput && timing.isFirstFrame(400));
  timing.onBufferReset();
  first = timing.startWithReadyPacket(400);
  assert(!first->configureOutput);
  timing.onArrival(ArrivalKind::first);
  assert(timing.startWithReadyPacket(500)->configureOutput);
  std::thread arrival([&] { for (unsigned i = 0; i < 1000; ++i) timing.onArrival(ArrivalKind::overflow); });
  for (unsigned i = 0; i < 1000; ++i) {
    auto start = timing.startWithReadyPacket(i + 1);
    assert(start);
    timing.planPreroll(*start, {1000000000, 1200000000, true}, {48000, false, 0});
  }
  arrival.join();
}
