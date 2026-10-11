#include "transport/buffered_tcp_transport.hpp"
#include <gtest/gtest.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <future>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

class BufferedTcp : public testing::Test {
protected:
  void SetUp() override {
    listener = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    ASSERT_GE(listener, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
    ASSERT_EQ(listen(listener, 8), 0);
    socklen_t size = sizeof(address);
    ASSERT_EQ(getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size), 0);
  }
  void TearDown() override { if (client >= 0) close(client); close(listener); }
  void connectClient() {
    client = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    ASSERT_GE(client, 0);
    ASSERT_EQ(connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
  }
  static auto descriptors() { return std::distance(std::filesystem::directory_iterator("/proc/self/fd"), std::filesystem::directory_iterator{}); }
  int listener = -1, client = -1;
  sockaddr_in address{};
};
TEST_F(BufferedTcp, LoopbackBytesLargerThanReceiveChunkDrainBeforeStickyEof) {
  BufferedTcpTransport transport(listener, 17, "buf_test");
  ASSERT_TRUE(transport.start());
  connectClient();
  std::vector<uint8_t> input(6000);
  for (std::size_t index = 0; index < input.size(); ++index) input[index] = index % 251;
  ASSERT_EQ(send(client, input.data(), input.size(), MSG_NOSIGNAL), static_cast<ssize_t>(input.size()));
  shutdown(client, SHUT_WR);
  std::vector<uint8_t> output(input.size());
  auto read = transport.readExact(output);
  EXPECT_EQ(read.status, ByteQueueStatus::complete);
  EXPECT_EQ(output, input);
  std::array<uint8_t,1> byte{};
  EXPECT_EQ(transport.readExact(byte).status, ByteQueueStatus::endOfStream);
  EXPECT_EQ(transport.readExact(byte).status, ByteQueueStatus::endOfStream);
  transport.join();
  transport.join();
  EXPECT_GE(fcntl(listener, F_GETFD), 0);
}
TEST_F(BufferedTcp, StopBeforeAcceptAndIdleReceiveJoinPromptlyWithoutClosingListener) {
  for (const bool connected : {false,true}) {
    BufferedTcpTransport transport(listener, 8);
    ASSERT_TRUE(transport.start());
    if (connected) connectClient();
    const auto before = std::chrono::steady_clock::now();
    ASSERT_TRUE(transport.requestStop());
    transport.join();
    transport.join();
    EXPECT_LT(std::chrono::steady_clock::now() - before, std::chrono::seconds(1));
    EXPECT_GE(fcntl(listener, F_GETFD), 0);
  }
}
TEST_F(BufferedTcp, StopWakesWorkerBlockedAppendingToFullQueue) {
  BufferedTcpTransport transport(listener, 1);
  ASSERT_TRUE(transport.start());
  connectClient();
  const std::array<uint8_t,4096> data{};
  ASSERT_EQ(send(client, data.data(), data.size(), MSG_NOSIGNAL), static_cast<ssize_t>(data.size()));
  std::array<uint8_t,1> byte{};
  ASSERT_EQ(transport.readExact(byte).status, ByteQueueStatus::complete);
  const auto before = std::chrono::steady_clock::now();
  ASSERT_TRUE(transport.requestStop());
  transport.join();
  EXPECT_LT(std::chrono::steady_clock::now() - before, std::chrono::seconds(1));
}
TEST_F(BufferedTcp, ThreadCreationFailureIsStickyAndDoesNotLeakOrJoinInvalidThread) {
  const auto before = descriptors();
  unsigned attempts = 0;
  {
    BufferedTcpTransport transport(listener, 8, {}, [&](pthread_t *, auto, void *) { ++attempts; return EAGAIN; });
    auto started = transport.start();
    ASSERT_FALSE(started);
    EXPECT_EQ(started.error(), EAGAIN);
    std::array<uint8_t,1> byte{};
    auto failure = transport.readExact(byte);
    EXPECT_EQ(failure.status, ByteQueueStatus::error);
    EXPECT_EQ(failure.errorCode, EAGAIN);
    EXPECT_EQ(transport.start().error(), EALREADY);
    transport.join();
  }
  EXPECT_EQ(attempts, 1u);
  EXPECT_EQ(descriptors(), before);
}
TEST_F(BufferedTcp, InvalidAndBlockingListenersFailWithoutChangingBorrowedFlags) {
  BufferedTcpTransport invalid(-1, 8);
  EXPECT_EQ(invalid.start().error(), EBADF);
  const auto flags = fcntl(listener, F_GETFL);
  ASSERT_EQ(fcntl(listener, F_SETFL, flags & ~O_NONBLOCK), 0);
  BufferedTcpTransport blocking(listener, 8);
  EXPECT_EQ(blocking.start().error(), EINVAL);
  EXPECT_EQ(fcntl(listener, F_GETFL), flags & ~O_NONBLOCK);
}
