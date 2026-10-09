#include "playback_timing.hpp"
#include <cassert>

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
}
