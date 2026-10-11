#include "transport/bounded_byte_queue.hpp"
#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <future>

TEST(BoundedByteQueue, FragmentedProducerAndOversizeExactReadProgressThroughSmallCapacity) {
  BoundedByteQueue queue(2);
  const std::array<std::uint8_t,6> input{1,2,3,4,5,6};
  std::promise<void> attempting;
  auto producer = std::async(std::launch::async, [&] {
    attempting.set_value();
    return queue.append(input);
  });
  attempting.get_future().wait();
  EXPECT_EQ(producer.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
  std::array<std::uint8_t,6> output{};
  const auto result = queue.readExact(output);
  EXPECT_EQ(result.status, ByteQueueStatus::complete);
  EXPECT_EQ(result.count, output.size());
  EXPECT_EQ(output, input);
  EXPECT_EQ(producer.get().count, input.size());
}
TEST(BoundedByteQueue, EmptyReadBlocksUntilAppendPublishesBytes) {
  BoundedByteQueue queue(2);
  std::promise<void> attempting;
  std::array<std::uint8_t,2> output{};
  auto reader = std::async(std::launch::async, [&] { attempting.set_value(); return queue.readExact(output); });
  attempting.get_future().wait();
  EXPECT_EQ(reader.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
  const std::array<std::uint8_t,2> input{9,8};
  queue.append(input);
  EXPECT_EQ(reader.get().status, ByteQueueStatus::complete);
  EXPECT_EQ(output, input);
}
TEST(BoundedByteQueue, EofAndErrorsDrainBufferedBytesThenRemainPermanent) {
  for (const bool error : {false,true}) {
    BoundedByteQueue queue(4);
    const std::array<std::uint8_t,3> input{1,2,3};
    queue.append(input);
    if (error) queue.fail(42); else queue.finish();
    queue.finish();
    std::array<std::uint8_t,2> output{};
    EXPECT_EQ(queue.readExact(output).status, ByteQueueStatus::complete);
    EXPECT_EQ(output, (std::array<std::uint8_t,2>{1,2}));
    auto partial = queue.readExact(output);
    EXPECT_EQ(partial.status, error ? ByteQueueStatus::error : ByteQueueStatus::endOfStream);
    EXPECT_EQ(partial.count, 1u);
    EXPECT_EQ(output[0], 3);
    EXPECT_EQ(partial.errorCode, error ? 42 : 0);
    auto terminal = queue.readExact(output);
    EXPECT_EQ(terminal.status, partial.status);
    EXPECT_EQ(terminal.errorCode, partial.errorCode);
    EXPECT_EQ(terminal.count, 0u);
  }
}
TEST(BoundedByteQueue, StopWakesEmptyReaderAndDiscardsBufferedBytes) {
  BoundedByteQueue queue(1);
  std::array<std::uint8_t,1> output{};
  auto reader = std::async(std::launch::async, [&] { return queue.readExact(output); });
  EXPECT_EQ(reader.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
  queue.stop();
  EXPECT_EQ(reader.get().status, ByteQueueStatus::stopped);
  EXPECT_EQ(queue.readExact(output).remaining, 0u);
}
TEST(BoundedByteQueue, StopWakesFullProducerWithoutAcceptingMoreBytes) {
  BoundedByteQueue queue(1);
  const std::array<std::uint8_t,1> byte{1};
  queue.append(byte);
  auto producer = std::async(std::launch::async, [&] { return queue.append(byte); });
  EXPECT_EQ(producer.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
  queue.stop();
  auto stopped = producer.get();
  EXPECT_EQ(stopped.status, ByteQueueStatus::stopped);
  EXPECT_EQ(stopped.count, 0u);
  EXPECT_EQ(stopped.remaining, 0u);
}
