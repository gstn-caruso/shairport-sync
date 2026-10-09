#include "volume_control.hpp"
#include <cassert>
#include <thread>

int main() {
  SharedVolumeLevel shared(-24);
  VolumeControl first, second;
  assert(first.suggestedLevel(shared) == -24);
  shared.remember(-15);
  assert(first.suggestedLevel(shared) == -15 && second.suggestedLevel(shared) == -15);
  first.rememberLevel(-30);
  shared.remember(0);
  assert(first.suggestedLevel(shared) == -30 && second.suggestedLevel(shared) == 0);
  first.apply({.gainFixed16 = 1234, .unmute = true}, false);
  assert(first.pcmSnapshot().gainFixed16 == 1234 && !first.pcmSnapshot().softwareMuted);
  first.apply({.requestMute = true}, false);
  assert(first.pcmSnapshot().softwareMuted);
  first.apply({}, false);
  assert(first.pcmSnapshot().softwareMuted && first.pcmSnapshot().gainFixed16 == 1234);
  first.apply({.gainFixed16 = 65536, .unmute = true}, false);
  first.apply({.requestMute = true}, true);
  assert(!first.pcmSnapshot().softwareMuted);
  std::thread setter([&] {
    for (int i = 0; i < 10000; ++i) {
      first.apply({.gainFixed16 = 11, .requestMute = true}, false);
      first.apply({.gainFixed16 = 22, .unmute = true}, false);
    }
  });
  for (int i = 0; i < 10000; ++i) {
    const auto pcm = first.pcmSnapshot();
    assert((pcm.gainFixed16 == 11 && pcm.softwareMuted) ||
           (pcm.gainFixed16 == 22 && !pcm.softwareMuted) || pcm.gainFixed16 == 65536);
  }
  setter.join();
}
