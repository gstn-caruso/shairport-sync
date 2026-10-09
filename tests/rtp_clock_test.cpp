#include "rtp_clock.hpp"
#include <cassert>

int main() {
  RtpClock clock;
  assert(clock.status() == clock_no_anchor_info);
  assert(clock.observe({clock_ok, 7, 1000000000, 100, 0}, 1000000000) == clock_no_anchor_info);
  assert(!clock.anchorFrame(44100, 0.0));
  assert(!clock.localTimeForFrame(100, 44100, 0.0));
  assert(!clock.frameForLocalTime(1000000000, 44100, 0.0));
  clock.setAnchor(7, 100, 9000000000, 1000000000);
  assert(clock.observe({clock_ok, 7, 1000000000, 100000000, 600000001}, 1000000000) == clock_not_valid);
  assert(clock.observe({clock_ok, 7, 1000000000, 100000000, 800000000}, 1000000000) == clock_not_valid);
  assert(!clock.localTimeForFrame(100, 44100, 0.0));
  assert(clock.observe({clock_ok, 7, 1000000000, 100000000, 600000000}, 1000000000) == clock_ok);
  assert(clock.anchorFrame(44100, 0.0) == 100);
  assert(clock.anchorFrame(44100, 0.1) == uint32_t(100 - 4410));
  assert(clock.localTimeForFrame(44200, 44100, 0.0) == 9900000000);
  assert(clock.frameForLocalTime(9900000000, 44100, 0.0) == 44200);
  assert(clock.localTimeForFrame(100, 48000, 0.1) == 9000000000);
  assert(!clock.localTimeForFrame(100, 0, 0.0));
  assert(!clock.frameForLocalTime(1000000000, 0, 0.0));
  clock.setAnchor(7, UINT32_MAX - 10, 9000000000, 7000000000);
  clock.observe({clock_ok, 7, 7000000000, 100000000, 600000000}, 7000000000);
  assert(clock.localTimeForFrame(9, 44100, 0.0) == 8900453514);
  assert(clock.frameForLocalTime(8900453514, 44100, 0.0) == 8);
  RtpClock fallback;
  fallback.setAnchor(7, 100, 9000000000, 1000000000);
  fallback.observe({clock_ok, 7, 1000000000, 100000000, 0}, 1000000000);
  assert(fallback.observe({clock_ok, 8, 4000000000, 300000000, 1000000000}, 4000000000) == clock_ok);
  assert(fallback.localTimeForFrame(100, 44100, 0.0) == 8900000000);
  assert(fallback.observe({clock_ok, 8, 6000000001, 300000000, 5000000000}, 6000000001) == clock_ok);
  fallback.observe({clock_ok, 8, 6100000000, 400000000, 5000000000}, 6100000000);
  assert(fallback.localTimeForFrame(100, 44100, 0.0) == 8800000000);
  assert(fallback.observe({clock_not_ready, 0, 0, 0, 0}, 6200000000) == clock_not_ready);
  assert(fallback.anchorFrame(44100, 0.0) == 100);
  assert(!fallback.localTimeForFrame(100, 44100, 0.0));
  fallback.reset();
  assert(fallback.status() == clock_no_anchor_info);
  assert(!fallback.anchorFrame(44100, 0.0));
  RtpClock changed;
  changed.setAnchor(7, 100, 9000000000, 1000000000);
  changed.observe({clock_ok, 7, 1000000000, 100000000, 0}, 1000000000);
  changed.setAnchor(7, 500, 9100000000, 2000000000);
  assert(!changed.anchorFrame(44100, 0.0));
  assert(changed.observe({clock_ok, 8, 2000000000, 300000000, 0}, 2000000000) == clock_not_valid);
  RtpClock boundary;
  boundary.setAnchor(7, 100, 9000000000, 1000000000);
  boundary.observe({clock_ok, 7, 1000000000, 100000000, 0}, 1000000000);
  boundary.observe({clock_ok, 8, 6000000000, 300000000, 5000000000}, 6000000000);
  boundary.observe({clock_ok, 7, 6100000000, 200000000, 0}, 6100000000);
  assert(boundary.localTimeForFrame(100, 44100, 0.0) == 8800000000);
}
