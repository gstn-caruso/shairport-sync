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
}
