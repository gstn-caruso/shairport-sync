#include "utilities/structured_buffer.hpp"
#include <gtest/gtest.h>
#include <cstdarg>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

static int format_into(StructuredBuffer &buffer, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = buffer.append_format(format, args);
  va_end(args);
  return result;
}

static_assert(!std::is_copy_constructible<StructuredBuffer>::value, "unique ownership");
static_assert(!std::is_copy_assignable<StructuredBuffer>::value, "unique ownership");

static void fillWithTextAndBinaryBytes(StructuredBuffer &buffer) {
  EXPECT_EQ(format_into(buffer, "%s", "hello"), 5);
  const char binary[] = {0, 'x'};
  EXPECT_EQ(buffer.append(binary, sizeof(binary)), 0);
}

TEST(StructuredBuffer, TextAndBinaryAppendsPreserveEmbeddedNull) {
  StructuredBuffer buffer(8);
  EXPECT_EQ(buffer.length(), 0);
  fillWithTextAndBinaryBytes(buffer);
  EXPECT_EQ(buffer.length(), 7);
  EXPECT_EQ(std::memcmp(buffer.data(), "hello\0x", 7), 0);
}

TEST(StructuredBuffer, FullBufferRejectsBinaryAppendAndFormatsNoFurtherText) {
  StructuredBuffer buffer(8);
  fillWithTextAndBinaryBytes(buffer);
  EXPECT_EQ(buffer.append("x", 1), -1);
  EXPECT_EQ(format_into(buffer, "%s", "full"), 0);
}

TEST(StructuredBuffer, ClearResetsLengthWithoutErasingBytes) {
  StructuredBuffer buffer(8);
  fillWithTextAndBinaryBytes(buffer);
  buffer.append("x", 1);
  format_into(buffer, "%s", "full");
  buffer.clear();
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_EQ(std::memcmp(buffer.data(), "hello\0x", 7), 0);
}

TEST(StructuredBuffer, LeadingNullFormattingAllowsFollowingTextToFillCapacity) {
  StructuredBuffer buffer(8);
  fillWithTextAndBinaryBytes(buffer);
  buffer.append("x", 1);
  format_into(buffer, "%s", "full");
  buffer.clear();
  EXPECT_EQ(format_into(buffer, "%c%s", 0, "tail"), 0);
  EXPECT_EQ(format_into(buffer, "%s", "123456789"), 7);
  EXPECT_EQ(buffer.length(), 7);
}

TEST(StructuredBuffer, ZeroCapacityFormattingPreservesBorrowedByte) {
  StructuredBuffer empty(0);
  empty.data()[0] = 'x';
  EXPECT_EQ(format_into(empty, "%s", "text"), 0);
  EXPECT_EQ(empty.length(), 0);
  EXPECT_EQ(empty.data()[0], 'x');
}

TEST(StructuredBuffer, UnrepresentableCapacityThrowsLengthError) {
  EXPECT_THROW(StructuredBuffer impossible(std::numeric_limits<std::size_t>::max()),
               std::length_error);
}
