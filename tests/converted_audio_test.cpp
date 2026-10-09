#include "converted_audio.hpp"
#include <gtest/gtest.h>

TEST(ConvertedAudio, MoveConstructionTransfersPcmAndEmptiesSource) {
  const NativePcmShape shape{2, 16, 16};
  auto allocation = ConvertedAudio::allocate(8, 2, 3, shape);
  ASSERT_TRUE(allocation);
  auto original = std::move(*allocation);
  original.bytes()[0] = 17;
  auto transferred = std::move(original);
  EXPECT_EQ(original.frames(), 0);
  EXPECT_EQ(original.retainedFrames(), 0);
  EXPECT_TRUE(original.bytes().empty());
  EXPECT_FALSE(original);
  EXPECT_EQ(transferred.frames(), 2);
  EXPECT_EQ(transferred.retainedFrames(), 3);
  EXPECT_EQ(transferred.shape(), shape);
  EXPECT_EQ(original.shape(), NativePcmShape{});
  ASSERT_EQ(transferred.bytes().size(), 8);
  EXPECT_EQ(transferred.bytes()[0], 17);
}

TEST(ConvertedAudio, MoveAssignmentPreservesPcmAndEmptiesSource) {
  const NativePcmShape shape{2, 16, 16};
  auto allocation = ConvertedAudio::allocate(8, 2, 3, shape);
  ASSERT_TRUE(allocation);
  auto original = std::move(*allocation);
  original.bytes()[0] = 17;
  auto transferred = std::move(original);
  ConvertedAudio replaced;
  replaced = std::move(transferred);
  EXPECT_TRUE(transferred.bytes().empty());
  ASSERT_FALSE(replaced.bytes().empty());
  EXPECT_EQ(replaced.bytes()[0], 17);
}

TEST(ConvertedAudio, RepeatedResetClearsTransferredPcmAndFrameCount) {
  const NativePcmShape shape{2, 16, 16};
  auto allocation = ConvertedAudio::allocate(8, 2, 3, shape);
  ASSERT_TRUE(allocation);
  auto original = std::move(*allocation);
  original.bytes()[0] = 17;
  auto transferred = std::move(original);
  ConvertedAudio replaced;
  replaced = std::move(transferred);
  replaced.reset();
  replaced.reset();
  EXPECT_TRUE(replaced.bytes().empty());
  EXPECT_EQ(replaced.frames(), 0);
}
