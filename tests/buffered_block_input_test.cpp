#include "protocol/ap2/buffered_block_input.hpp"
#include "transport/bounded_byte_queue.hpp"
#include "transport/buffered_tcp_transport.hpp"
#include "buffered_block_fixture.hpp"
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

TEST(BufferedBlockInput, RealTcpFragmentedBodyLargerThanReceiveChunk) {
  const int listener = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  ASSERT_GE(listener, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
  ASSERT_EQ(listen(listener, 1), 0);
  socklen_t size = sizeof(address);
  ASSERT_EQ(getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size), 0);
  {
    BufferedTcpTransport transport(listener, 31);
    ASSERT_TRUE(transport.start());
    const int client = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(client, 0);
    ASSERT_EQ(connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
    std::vector<uint8_t> body(6000);
    for (size_t index = 0; index < body.size(); ++index) body[index] = index % 251;
    const std::array<uint8_t,2> prefix{0x17,0x72};
    ASSERT_EQ(send(client, prefix.data(), 1, MSG_NOSIGNAL), 1);
    ASSERT_EQ(send(client, prefix.data()+1, 1, MSG_NOSIGNAL), 1);
    ASSERT_EQ(send(client, body.data(), 3000, MSG_NOSIGNAL), 3000);
    ASSERT_EQ(send(client, body.data()+3000, 3000, MSG_NOSIGNAL), 3000);
    shutdown(client, SHUT_WR);
    std::array<uint8_t,16384> storage{};
    auto result = readBufferedAudioBlock(transport, storage);
    ASSERT_EQ(result.status, BufferedBlockReadStatus::complete);
    EXPECT_EQ(result.count, body.size());
    EXPECT_TRUE(std::equal(body.begin(), body.end(), storage.begin()));
    EXPECT_EQ(readBufferedAudioBlock(transport, storage).status, BufferedBlockReadStatus::channelClosed);
    close(client);
  }
  close(listener);
}

TEST(BufferedBlockInput, ValidPrefixReadsOnlyDeclaredBodyAndReportsOccupancy) {
  BoundedByteQueue input(128);
  std::vector<uint8_t> stream{0,54};
  stream.insert(stream.end(), buffered_block_fixture::golden.begin(), buffered_block_fixture::golden.end());
  stream.resize(102);
  input.append(stream);
  std::array<uint8_t,16384> storage{};
  auto result = readBufferedAudioBlock(input, storage);
  EXPECT_EQ(result.status, BufferedBlockReadStatus::complete);
  EXPECT_EQ(result.count, 52u);
  EXPECT_EQ(result.prefixRemaining, 100u);
  EXPECT_EQ(result.bodyRemaining, 48u);
  EXPECT_TRUE(std::equal(buffered_block_fixture::golden.begin(), buffered_block_fixture::golden.end(), storage.begin()));
}
TEST(BufferedBlockInput, InvalidDeclaredLengthsLeaveBodyUnreadAndStorageUntouched) {
  std::vector<uint16_t> lengths;
  for (uint16_t length = 0; length < 38; ++length) lengths.push_back(length);
  lengths.insert(lengths.end(), {16387,65535});
  for (auto length : lengths) {
    SCOPED_TRACE(length);
    BoundedByteQueue input(128);
    std::vector<uint8_t> stream{static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length)};
    stream.insert(stream.end(), buffered_block_fixture::golden.begin(), buffered_block_fixture::golden.end());
    input.append(stream);
    input.finish();
    std::array<uint8_t,16384> storage;
    storage.fill(0x5a);
    auto result = readBufferedAudioBlock(input, storage);
    EXPECT_EQ(result.status, BufferedBlockReadStatus::invalidSize);
    EXPECT_FALSE(result.bodyRemaining);
    EXPECT_TRUE(std::all_of(storage.begin(), storage.end(), [](auto byte) { return byte == 0x5a; }));
    std::array<uint8_t,52> unread{};
    EXPECT_EQ(input.readExact(unread).status, ByteQueueStatus::complete);
    EXPECT_EQ(unread, buffered_block_fixture::golden);
  }
}
TEST(BufferedBlockInput, ShortPrefixAndInsufficientDestinationRejectWithoutBodyConsumption) {
  BoundedByteQueue shortPrefix(8);
  const std::array<uint8_t,1> byte{};
  shortPrefix.append(byte);
  shortPrefix.finish();
  std::array<uint8_t,16384> storage{};
  EXPECT_EQ(readBufferedAudioBlock(shortPrefix, storage).status, BufferedBlockReadStatus::invalidSize);
  BoundedByteQueue input(128);
  std::vector<uint8_t> stream{0,54};
  stream.insert(stream.end(), buffered_block_fixture::golden.begin(), buffered_block_fixture::golden.end());
  input.append(stream);
  auto result = readBufferedAudioBlock(input, std::span(storage).first(51));
  EXPECT_EQ(result.status, BufferedBlockReadStatus::invalidSize);
  EXPECT_EQ(result.prefixRemaining, 52u);
  std::array<uint8_t,52> unread{};
  EXPECT_EQ(input.readExact(unread).status, ByteQueueStatus::complete);
  EXPECT_EQ(unread, buffered_block_fixture::golden);
}
TEST(BufferedBlockInput, PartialBodyAndStickyErrorRemainExplicit) {
  BoundedByteQueue input(8);
  const std::array<uint8_t,4> stream{0,54,1,2};
  input.append(stream);
  input.fail(42);
  std::array<uint8_t,16384> storage{};
  auto result = readBufferedAudioBlock(input, storage);
  EXPECT_EQ(result.status, BufferedBlockReadStatus::readError);
  EXPECT_EQ(result.count, 2u);
  EXPECT_EQ(result.errorCode, 42);
  EXPECT_EQ(readBufferedAudioBlock(input, storage).errorCode, 42);
}
