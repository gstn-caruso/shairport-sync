#include "transport/bounded_byte_queue.hpp"
#include <gtest/gtest.h>
#include <array>

TEST(BufferedTransportLegacy, WrappedBytesAreReadExactlyAndReportRemainingOccupancy) {
  BoundedByteQueue queue(8);
  const std::array<std::uint8_t, 6> filler{};
  std::array<std::uint8_t, 6> consumed{};
  queue.append(filler);
  queue.readExact(consumed);
  const std::array<std::uint8_t,6> input{'A','B','C','D','E','F'};
  queue.append(input);
  std::array<std::uint8_t,4> bytes{};
  auto read = queue.readExact(bytes);
  EXPECT_EQ(read.status, ByteQueueStatus::complete);
  EXPECT_EQ(read.count, 4u);
  EXPECT_EQ(bytes, (std::array<std::uint8_t,4>{'A','B','C','D'}));
  EXPECT_EQ(read.remaining, 2u);
}
TEST(BufferedTransportLegacy, ClosedReaderMustReturnWithoutWaitingForAnotherSignal) {
  BoundedByteQueue queue(8);
  queue.finish();
  std::array<std::uint8_t,1> byte{};
  EXPECT_EQ(queue.readExact(byte).status, ByteQueueStatus::endOfStream);
  EXPECT_EQ(queue.readExact(byte).status, ByteQueueStatus::endOfStream);
}
