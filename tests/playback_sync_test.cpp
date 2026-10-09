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
}
