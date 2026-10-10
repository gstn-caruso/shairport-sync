#include "audio/output/pcm_output_queue.hpp"
#include <gtest/gtest.h>
#include <array>

TEST(PcmOutputQueue, BoundedFramesWrapInFifoOrder) {
  PcmOutputQueue queue(3, 2);
  std::array<std::byte, 8> input{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
  EXPECT_EQ(queue.enqueue(input), 6u);
  EXPECT_EQ(queue.enqueue(input), 0u);
  std::array<std::byte, 4> output{};
  EXPECT_EQ(queue.copyTo(output), 4u);
  EXPECT_EQ(output[0], std::byte{1});
  queue.consume(4);
  EXPECT_EQ(queue.enqueue(std::span(input).subspan(6)), 2u);
  EXPECT_EQ(queue.copyTo(output), 4u);
  EXPECT_EQ(output, (std::array<std::byte, 4>{std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}}));
  EXPECT_EQ(queue.occupiedFrames(), 2u);
  queue.clear();
  EXPECT_EQ(queue.occupiedFrames(), 0u);
}

TEST(PcmOutputQueue, TransfersOnlyWholeFrames) {
  PcmOutputQueue queue(2, 4);
  std::array<std::byte, 7> input{};
  EXPECT_EQ(queue.enqueue({}), 0u);
  EXPECT_EQ(queue.enqueue(input), 4u);
  EXPECT_EQ(queue.copyTo(std::span(input).first(3)), 0u);
  queue.consume(3);
  EXPECT_EQ(queue.occupiedFrames(), 1u);
  queue.consume(8);
  EXPECT_EQ(queue.occupiedFrames(), 0u);
}
