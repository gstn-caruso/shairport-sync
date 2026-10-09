#include "playback_sync.hpp"
#include <cassert>

int main() {
  PlaybackSync sync;
  SyncObservation observation;
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
  assert(initial.errorFrames == 44 && initial.correctionFrames == 0);
  assert(sync.skipFrom(352) == 44);
  sync.resetForPlay();
  observation.firstFrame = false;
  observation.inputRate = observation.outputRate = 1000;
  observation.expectedFrameTime = observation.dacMeasurementTime;
  observation.retainedFrames = 17;
  observation.measurementDuration = 2000000;
  assert(sync.observe(observation, {true, 0, 0}).errorFrames == 0);
  observation.retainedFrames = 0;
  observation.measurementDuration = 0;
  auto retained = sync.observe(observation, {true, 0, 0});
  assert(retained.errorFrames == 17 && retained.errorNs == 17000000);
  assert(sync.observe(observation, {true, 0, 0}).errorFrames == 0);
  sync.resetForPlay();
  observation.measured = false;
  observation.retainedFrames = 17;
  assert(sync.observe(observation, {true, 0, 0}).correctionFrames == 0);
  observation.measured = true;
  observation.retainedFrames = 0;
  assert(sync.observe(observation, {true, 0, 0}).errorFrames == 0);
  observation.outputRate = 0;
  assert(sync.observe(observation, {true, 0, 0}).correctionFrames == 0);
  sync.resetForPlay();
  observation.outputRate = observation.inputRate = 1000;
  observation.firstFrame = true;
  observation.expectedFrameTime = observation.dacMeasurementTime + 5000000;
  auto early = sync.observe(observation, {false, 0, 0});
  assert(early.silenceFrames == 5 && early.errorFrames == 0);
  observation.expectedFrameTime = observation.dacMeasurementTime - 100000000;
  auto late = sync.observe(observation, {false, 0, 0});
  assert(late.errorFrames == 100);
  assert(sync.skipFrom(30) == 30);
  assert(sync.skipFrom(30) == 30);
  assert(sync.skipFrom(100) == 40);
  assert(sync.skipFrom(100) == 0);
  sync.resetForPlay();
  observation.firstFrame = false;
  observation.expectedFrameTime = observation.dacMeasurementTime;
  observation.timestampGap = -352;
  observation.inputRate = 44100;
  observation.outputRate = 48000;
  sync.observe(observation, {false, 0, 0});
  assert(sync.skipFrom(200) == 200 && sync.skipFrom(200) == 183);
  sync.resetForPlay();
  observation.timestampGap = 0;
  observation.inputRate = observation.outputRate = 1000;
  observation.firstFrame = true;
  sync.observe(observation, {false, 0, 0});
  assert(sync.skipFrom(352) == 0);
  observation.firstFrame = false;
  observation.expectedFrameTime -= 1000000;
  sync.observe(observation, {false, 0, 0});
  assert(sync.skipFrom(352) == 1);
  sync.resetForPlay();
  observation.firstFrame = false;
  observation.blockFrames = 512;
  observation.playNumber = 19;
  observation.expectedFrameTime = observation.dacMeasurementTime - 2000000;
  assert(sync.observe(observation, {true, 1000000, 0}).correctionFrames == 0);
  observation.playNumber = 20;
  assert(sync.observe(observation, {true, 1000000, 0}).correctionFrames == -1);
  assert(sync.observe(observation, {false, 1000000, 1e-9}).correctionFrames == 0);
  for (int sign : {-1, 1}) {
    sync.resetForPlay();
    observation.expectedFrameTime = observation.dacMeasurementTime - sign * int64_t{2000000};
    for (unsigned index = 0; index < 39; ++index)
      assert(!sync.observe(observation, {true, 2000000, 0.001999999}).dropPacket);
    auto exact = sync.observe(observation, {true, 2000000, 0.002});
    assert(exact.correctionFrames == 0 && !exact.dropPacket && exact.windowSpreadNs == 0);
    auto exceeded = sync.observe(observation, {true, 1999999, 0.001999999});
    assert(exceeded.dropPacket && exceeded.resyncNext == (sign < 0));
    assert(exceeded.correctionFrames == -sign);
  }
  sync.resetForPlay();
  for (int64_t error : {4000000, 2000000, 6000000, 3000000}) {
    observation.expectedFrameTime = observation.dacMeasurementTime - error;
    auto current = sync.observe(observation, {true, 3500000, 0});
    if (error == 3000000)
      assert(current.windowSpreadNs == 1000000 && current.correctionFrames == 0);
  }
  assert(sync.observe(observation, {true, 3499999, 0}).correctionFrames == -1);
  sync.resetForPlay();
  observation.expectedFrameTime = observation.dacMeasurementTime - 2000000;
  for (unsigned index = 0; index < 40; ++index)
    sync.observe(observation, {true, 1000000, 0});
  observation.firstFrame = true;
  sync.observe(observation, {true, 1000000, 0.001});
  assert(sync.skipFrom(352) == 2);
  observation.firstFrame = false;
  observation.inputRate = observation.outputRate = 2000;
  assert(sync.observe(observation, {true, 1000000, 0.001}).dropPacket);
  observation.expectedFrameTime = observation.dacMeasurementTime + 2000000;
  assert(!sync.observe(observation, {true, 1000000, 0.001}).dropPacket);
  sync.resetForPlay();
  assert(!sync.observe(observation, {true, 1000000, 0.001}).dropPacket);
  sync.resetForPlay();
  observation.firstFrame = false;
  observation.inputRate = observation.outputRate = 1000;
  observation.expectedFrameTime = UINT64_MAX - 499999;
  observation.dacMeasurementTime = 500000;
  auto wrapped = sync.observe(observation, {true, 1000000, 0});
  assert(wrapped.errorNs == 1000000 && wrapped.errorFrames == 1);
}
