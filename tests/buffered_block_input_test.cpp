#include "protocol/ap2/buffered_block_input.hpp"
#include "buffered_block_fixture.hpp"
#include <gtest/gtest.h>
#include <cstring>
#include <arpa/inet.h>

namespace {
struct ReadScript {
  std::uint16_t declaredLength = 54;
  ssize_t prefixCount = 2;
  unsigned calls = 0;
  std::size_t bodyRequested = 0;
};
thread_local ReadScript *script = nullptr;
class BufferedBlockInput : public testing::Test {
protected:
  void SetUp() override { script = &current; storage.fill(0x5a); }
  void TearDown() override { script = nullptr; }
  ReadScript current;
  buffered_tcp_desc input{};
  std::array<std::uint8_t, 16384> storage{};
};
}
extern "C" ssize_t __wrap_read_sized_block(buffered_tcp_desc *, void *destination,
                                           size_t count, size_t *remaining) {
  ++script->calls;
  if (script->calls == 1) {
    const auto length = htons(script->declaredLength);
    std::memcpy(destination, &length, sizeof(length));
    *remaining = 100;
    return script->prefixCount;
  }
  script->bodyRequested = count;
  *remaining = 48;
  if (count > buffered_block_fixture::golden.size())
    return 0;
  std::memcpy(destination, buffered_block_fixture::golden.data(), count);
  return count;
}

TEST_F(BufferedBlockInput, ValidPrefixReadsOnlyDeclaredBodyAndReportsOccupancy) {
  auto result = readBufferedAudioBlock(input, storage);
  EXPECT_EQ(result.status, BufferedBlockReadStatus::complete);
  EXPECT_EQ(result.count, 52u);
  EXPECT_EQ(current.calls, 2u);
  EXPECT_EQ(current.bodyRequested, 52u);
  EXPECT_EQ(result.prefixRemaining, 100u);
  EXPECT_EQ(result.bodyRemaining, 48u);
  EXPECT_TRUE(std::equal(buffered_block_fixture::golden.begin(), buffered_block_fixture::golden.end(), storage.begin()));
}

TEST_F(BufferedBlockInput, InvalidDeclaredLengthsAreRejectedBeforeAnyBodyRead) {
  std::vector<std::uint16_t> lengths;
  for (std::uint16_t length = 0; length < 38; ++length)
    lengths.push_back(length);
  lengths.insert(lengths.end(), {16387, 65535});
  for (auto length : lengths) {
    SCOPED_TRACE(length);
    current = {};
    current.declaredLength = length;
    auto result = readBufferedAudioBlock(input, storage);
    EXPECT_EQ(result.status, BufferedBlockReadStatus::invalidSize);
    EXPECT_EQ(current.calls, 1u);
    EXPECT_FALSE(result.bodyRemaining);
    EXPECT_TRUE(std::all_of(storage.begin(), storage.end(), [](auto byte) { return byte == 0x5a; }));
  }
}

TEST_F(BufferedBlockInput, ShortPrefixAndInsufficientDestinationRejectBeforeBodyRead) {
  current.prefixCount = 1;
  EXPECT_EQ(readBufferedAudioBlock(input, storage).status, BufferedBlockReadStatus::invalidSize);
  EXPECT_EQ(current.calls, 1u);
  current = {};
  EXPECT_EQ(readBufferedAudioBlock(input, std::span(storage).first(51)).status, BufferedBlockReadStatus::invalidSize);
  EXPECT_EQ(current.calls, 1u);
}
