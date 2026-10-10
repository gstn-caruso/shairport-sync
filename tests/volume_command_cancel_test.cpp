#include "session/session_state.hpp"
#include "audio/output/audio_player_adapter.hpp"
#include "volume/volume_runtime.hpp"
#include <gtest/gtest.h>
#include <cerrno>
#include <csignal>
#include <format>
#include <filesystem>
#include <cstring>
#include <functional>
#include <sys/wait.h>
#include <unistd.h>

void *player_thread_func(void *);
extern "C" void __wrap_ptp_send_control_message_string(const char *) {}
static void *idleReceiver(void *) { for (;;) pause(); }
static void *setVolume(void *argument) {
  applySessionVolume(AirPlayVolume{-15}, *static_cast<SessionState *>(argument));
  pthread_testcancel();
  return nullptr;
}
static int chooseOutput(unsigned, unsigned, unsigned) { return 0; }
static std::string selfExecutable;

static void checkCancellation(const char *executable, bool startup) {
  const auto savedCommand = config.cmd_set_volume;
  const auto savedBlocking = config.cmd_blocking;
  const auto savedOutput = config.output;
  const auto savedSharedLevel = sharedVolumeLevel.current();
  struct RestoreConfiguration {
    std::function<void()> restore;
    ~RestoreConfiguration() { restore(); }
  } restore{[&] {
    config.cmd_set_volume = savedCommand;
    config.cmd_blocking = savedBlocking;
    config.output = savedOutput;
    sharedVolumeLevel.remember(savedSharedLevel);
  }};
  int ready[2], release[2];
  ASSERT_EQ(pipe(ready), 0);
  const auto releaseCreated = pipe(release);
  if (releaseCreated != 0) {
    close(ready[0]);
    close(ready[1]);
  }
  ASSERT_EQ(releaseCreated, 0);
  struct OwnedPipes {
    int *ready, *release;
    ~OwnedPipes() {
      for (int fd : {ready[0], ready[1], release[0], release[1]})
        EXPECT_EQ(close(fd), 0);
    }
  } pipes{ready, release};
  const auto command = std::format("{} --child {} {}", executable, ready[1], release[0]);
  config.cmd_set_volume = const_cast<char *>(command.c_str());
  config.cmd_blocking = 1;
  audio_output backend{};
  backend.get_configuration = chooseOutput;
  config.output = &backend;
  sharedVolumeLevel.remember(AirPlayVolume{0});
  SessionState session{};
  session.airplay_stream_type = realtime_stream;
  ASSERT_EQ(pthread_mutex_init(&session.flush_mutex, nullptr), 0);
  struct OwnedMutex {
    pthread_mutex_t &mutex;
    ~OwnedMutex() { EXPECT_EQ(pthread_mutex_destroy(&mutex), 0); }
  } mutex{session.flush_mutex};
  if (startup) {
    ASSERT_EQ(pthread_create(&session.rtp_realtime_audio_thread, nullptr, idleReceiver, nullptr), 0);
    const auto controlStarted = pthread_create(&session.rtp_ap2_control_thread, nullptr, idleReceiver, nullptr);
    if (controlStarted != 0) {
      pthread_cancel(session.rtp_realtime_audio_thread);
      pthread_join(session.rtp_realtime_audio_thread, nullptr);
    }
    ASSERT_EQ(controlStarted, 0);
  }
  pthread_t worker;
  const auto started = pthread_create(&worker, nullptr, startup ? player_thread_func : setVolume, &session);
  if (started != 0 && startup) {
    pthread_cancel(session.rtp_realtime_audio_thread);
    pthread_cancel(session.rtp_ap2_control_thread);
    pthread_join(session.rtp_realtime_audio_thread, nullptr);
    pthread_join(session.rtp_ap2_control_thread, nullptr);
  }
  ASSERT_EQ(started, 0);
  pid_t child = -1;
  EXPECT_EQ(read(ready[0], &child, sizeof child), sizeof child);
  EXPECT_EQ(pthread_cancel(worker), 0);
  timespec deadline{};
  EXPECT_EQ(clock_gettime(CLOCK_REALTIME, &deadline), 0);
  ++deadline.tv_sec;
  void *completion = nullptr;
  const int joined = pthread_timedjoin_np(worker, &completion, &deadline);
  EXPECT_GT(child, 0);
  if (child > 0)
    EXPECT_EQ(kill(child, SIGKILL), 0);
  if (joined != 0)
    EXPECT_EQ(pthread_join(worker, &completion), 0);
  if (child > 0) {
    const auto reaped = waitpid(child, nullptr, 0);
    const auto reapError = errno;
    if (reaped == -1)
      EXPECT_EQ(reapError, ECHILD);
    else
      EXPECT_EQ(reaped, child);
  }
  config.cmd_set_volume = nullptr;
  EXPECT_EQ(joined, 0);
  EXPECT_EQ(completion, PTHREAD_CANCELED);
  EXPECT_EQ(session.volumeControl.pcmSnapshot().gainFixed16, FixedGain16{65536});
}

TEST(VolumeCommandCancellation, StartupCancellationLeavesAppliedGainAndReapsCommand) {
  checkCancellation(selfExecutable.c_str(), true);
}

TEST(VolumeCommandCancellation, UpdateCancellationLeavesAppliedGainAndReapsCommand) {
  checkCancellation(selfExecutable.c_str(), false);
}

int main(int argc, char **argv) {
  if (argc > 1 && std::strcmp(argv[1], "--child") == 0) {
    if (argc < 4) return 1;
    const int ready = std::atoi(argv[2]), release = std::atoi(argv[3]);
    const pid_t self = getpid();
    if (write(ready, &self, sizeof self) != sizeof self) return 1;
    char byte;
    return read(release, &byte, 1) == 1 ? 0 : 1;
  }
  selfExecutable = std::filesystem::absolute(argv[0]).string();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
