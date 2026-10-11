#include "session/session_state.hpp"
#include "protocol/rtp/rtp.h"
#include <gtest/gtest.h>
#include <future>
#include <filesystem>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static std::promise<void> *processorReady;
extern "C" int __wrap_have_ptp_timing_information(rtsp_conn_info *) { return 1; }
extern "C" void __wrap_reset_buffer(rtsp_conn_info *) { processorReady->set_value(); }
extern "C" void *rtp_buffered_audio_processor(void *);

TEST(BufferedProcessorLifecycle, CancelRealProcessorJoinsTransportAndClosesBorrowedListener) {
  const auto descriptors = [] { return std::distance(std::filesystem::directory_iterator("/proc/self/fd"), std::filesystem::directory_iterator{}); };
  SessionState session;
  session.connection_number = 23;
  session.ap2_audio_buffer_size = 17;
  session.ap2_play_enabled = 1;
  const auto before = descriptors();
  const int listener = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  ASSERT_GE(listener, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
  ASSERT_EQ(listen(listener, 1), 0);
  session.buffered_audio_socket = listener;
  std::promise<void> ready;
  processorReady = &ready;
  auto entered = ready.get_future();
  pthread_t processor;
  ASSERT_EQ(pthread_create(&processor, nullptr, rtp_buffered_audio_processor, &session), 0);
  ASSERT_EQ(entered.wait_for(std::chrono::seconds(1)), std::future_status::ready);
  ASSERT_EQ(pthread_cancel(processor), 0);
  void *result = nullptr;
  ASSERT_EQ(pthread_join(processor, &result), 0);
  EXPECT_EQ(result, PTHREAD_CANCELED);
  EXPECT_EQ(session.buffered_audio_socket, -1);
  EXPECT_EQ(fcntl(listener, F_GETFD), -1);
  EXPECT_EQ(errno, EBADF);
  EXPECT_EQ(descriptors(), before);
  processorReady = nullptr;
}
