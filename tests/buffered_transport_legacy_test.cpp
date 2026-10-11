#include "platform/utilities/buffered_read.h"
#include <gtest/gtest.h>
#include <array>

TEST(BufferedTransportLegacy, WrappedBytesAreReadExactlyAndReportRemainingOccupancy) {
  buffered_tcp_desc input{};
  std::array<char, 8> ring{'C','D','E','F',0,0,'A','B'};
  input.buffer = ring.data();
  input.buffer_max_size = ring.size();
  input.buffer_occupancy = 6;
  input.toq = ring.data() + 6;
  input.eoq = ring.data() + 4;
  ASSERT_EQ(pthread_mutex_init(&input.mutex, nullptr), 0);
  ASSERT_EQ(pthread_cond_init(&input.not_empty_cv, nullptr), 0);
  ASSERT_EQ(pthread_cond_init(&input.not_full_cv, nullptr), 0);
  std::array<char, 4> bytes{};
  std::size_t remaining = 0;
  EXPECT_EQ(read_sized_block(&input, bytes.data(), bytes.size(), &remaining), 4);
  EXPECT_EQ(bytes, (std::array<char,4>{'A','B','C','D'}));
  EXPECT_EQ(remaining, 2u);
  EXPECT_EQ(pthread_cond_destroy(&input.not_empty_cv), 0);
  EXPECT_EQ(pthread_cond_destroy(&input.not_full_cv), 0);
  EXPECT_EQ(pthread_mutex_destroy(&input.mutex), 0);
}
