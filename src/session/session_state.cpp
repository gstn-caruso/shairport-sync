#include "session/session_state.hpp"
#include <unistd.h>

SessionState::~SessionState() {
  playbackRun.stop();
  if (fd >= 0)
    close(fd);
}
