#include "protocol/rtsp/rtsp_listener.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <future>
#include <atomic>
#include <poll.h>
#include <semaphore>
#include <sys/socket.h>
#include <unistd.h>

using namespace std::chrono_literals;

static std::atomic<bool> observeWait{false};
static std::binary_semaphore waiting{0};

extern "C" int __real_poll(pollfd *, nfds_t, int);
extern "C" int __wrap_poll(pollfd *descriptors, nfds_t count, int timeout) {
  if (observeWait.exchange(false))
    waiting.release();
  return __real_poll(descriptors, count, timeout);
}

TEST(RtspListener, StopWakesIdleAcceptAndJoins) {
  RtspListener listener;
  std::promise<int> result;
  auto finished = result.get_future();
  observeWait = true;
  listener.start([&](std::stop_token stop) {
    sockaddr_storage remote{};
    socklen_t size = sizeof(remote);
    result.set_value(listener.accept(stop, reinterpret_cast<sockaddr *>(&remote), &size));
  });
  waiting.acquire();
  EXPECT_EQ(finished.wait_for(20ms), std::future_status::timeout);
  listener.requestStop();
  ASSERT_EQ(finished.wait_for(500ms), std::future_status::ready);
  EXPECT_EQ(finished.get(), -1);
  listener.stop();
}
