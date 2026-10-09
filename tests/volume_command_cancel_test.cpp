#include "session_state.hpp"
#include "audio_player_adapter.hpp"
#include "volume_runtime.hpp"
#include <cassert>
#include <cerrno>
#include <csignal>
#include <format>
#include <sys/wait.h>
#include <unistd.h>

void *player_thread_func(void *);
extern "C" void __wrap_ptp_send_control_message_string(const char *) {}
static void *idleReceiver(void *) { for (;;) pause(); }
static void *setVolume(void *argument) {
  applySessionVolume(-15, *static_cast<SessionState *>(argument));
  pthread_testcancel();
  return nullptr;
}
static int chooseOutput(unsigned, unsigned, unsigned) { return 0; }

static void checkCancellation(const char *executable, bool startup) {
  int ready[2], release[2];
  assert(pipe(ready) == 0 && pipe(release) == 0);
  const auto command = std::format("{} --child {} {}", executable, ready[1], release[0]);
  config.cmd_set_volume = const_cast<char *>(command.c_str());
  config.cmd_blocking = 1;
  audio_output backend{};
  backend.get_configuration = chooseOutput;
  config.output = &backend;
  sharedVolumeLevel.remember(0);
  SessionState session{};
  session.airplay_stream_type = realtime_stream;
  assert(pthread_mutex_init(&session.flush_mutex, nullptr) == 0);
  if (startup) {
    assert(pthread_create(&session.rtp_realtime_audio_thread, nullptr, idleReceiver, nullptr) == 0);
    assert(pthread_create(&session.rtp_ap2_control_thread, nullptr, idleReceiver, nullptr) == 0);
  }
  pthread_t worker;
  assert(pthread_create(&worker, nullptr, startup ? player_thread_func : setVolume, &session) == 0);
  pid_t child;
  assert(read(ready[0], &child, sizeof child) == sizeof child);
  assert(pthread_cancel(worker) == 0);
  timespec deadline;
  assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
  ++deadline.tv_sec;
  void *completion = nullptr;
  const int joined = pthread_timedjoin_np(worker, &completion, &deadline);
  assert(kill(child, SIGKILL) == 0);
  if (joined != 0) assert(pthread_join(worker, &completion) == 0);
  const auto reaped = waitpid(child, nullptr, 0);
  assert(reaped == child || (reaped == -1 && errno == ECHILD));
  for (int fd : {ready[0], ready[1], release[0], release[1]}) assert(close(fd) == 0);
  config.cmd_set_volume = nullptr;
  assert(pthread_mutex_destroy(&session.flush_mutex) == 0);
  assert(joined == 0 && completion == PTHREAD_CANCELED);
  assert(session.volumeControl.pcmSnapshot().gainFixed16 == 65536);
}
int main(int argc, char **argv) {
  if (argc > 1) {
    const int ready = std::atoi(argv[2]), release = std::atoi(argv[3]);
    const pid_t self = getpid();
    if (write(ready, &self, sizeof self) != sizeof self) return 1;
    char byte;
    return read(release, &byte, 1) == 1 ? 0 : 1;
  }
  checkCancellation(argv[0], true);
  checkCancellation(argv[0], false);
}
