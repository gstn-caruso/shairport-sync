#include "rtp_clock.hpp"
#include <cassert>

int main() {
  RtpClock clock;
  assert(clock.status() == clock_no_anchor_info);
  assert(clock.observe({clock_ok, 7, 1000000000, 100, 0}, 1000000000) == clock_no_anchor_info);
  assert(!clock.anchorFrame(44100, 0.0));
  assert(!clock.localTimeForFrame(100, 44100, 0.0));
  assert(!clock.frameForLocalTime(1000000000, 44100, 0.0));
}
