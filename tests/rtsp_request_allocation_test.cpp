#include "protocol/rtsp/rtsp_message.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <cstring>
#include <new>
#include <span>
#include <string>
import receiver.protocol.rtsp.request;

namespace {
struct AllocationProbe {
  std::size_t poisonSize = 0;
  bool poisoned = false;
};
thread_local AllocationProbe *allocationProbe = nullptr;
class ProbeScope {
public:
  explicit ProbeScope(AllocationProbe &probe) { allocationProbe = &probe; }
  ~ProbeScope() { allocationProbe = nullptr; }
};
}

extern "C" void *__real__Znwm(std::size_t size);
extern "C" void *__real__Znam(std::size_t size);
static void *poisonAllocation(void *storage, std::size_t size) {
  if (allocationProbe && size == allocationProbe->poisonSize) {
    std::memset(storage, 0x5a, size);
    allocationProbe->poisoned = true;
  }
  return storage;
}
extern "C" void *__wrap__Znwm(std::size_t size) { return poisonAllocation(__real__Znwm(size), size); }
extern "C" void *__wrap__Znam(std::size_t size) { return poisonAllocation(__real__Znam(size), size); }

namespace {
class AllocationClock : public RtspRequestClock {
public:
  std::uint64_t nowNs() override { return 0; }
};
class AllocationEffects : public RtspRequestEffects {
public:
  void diagnostic(RtspRequestDiagnostic, RtspRequestPhase, int) override {}
  void closeHeaderChannel() override {}
  void stalled() override {}
};
class GrowingBodyInput : public RtspRequestInput {
public:
  bool stopped() override { return false; }
  RtspRequestRead read(std::span<char> destination) override {
    if (!headersRead) {
      std::copy(headers.begin(), headers.end(), destination.begin());
      headersRead = true;
      return {static_cast<std::ptrdiff_t>(headers.size()), 0, false};
    }
    untouchedTail = destination.back() == 0x5a;
    std::fill(destination.begin(), destination.end(), 'C');
    return {static_cast<std::ptrdiff_t>(destination.size()), 0, false};
  }
  std::string headers = "POST /feedback RTSP/1.0\r\nContent-Length: 5000\r\n\r\nAB";
  bool headersRead = false;
  bool untouchedTail = false;
};
}

TEST(RtspRequestAllocation, GrowingBodyDoesNotInitializeUnreadStorage) {
  GrowingBodyInput input;
  AllocationClock clock;
  AllocationEffects effects;
  RtspRequestReader reader(input, clock, effects);
  AllocationProbe probe{5000};
  RtspRequestResult result;
  {
    ProbeScope scope(probe);
    result = reader.read();
  }
  ASSERT_TRUE(probe.poisoned);
  EXPECT_TRUE(input.untouchedTail);
  ASSERT_EQ(result.status, RtspRequestStatus::success);
  ASSERT_TRUE(result.message);
  EXPECT_TRUE(result.message->bodyStartsWith("AB"));
  EXPECT_EQ(result.message->bodyLength(), 5000u);
  EXPECT_EQ(result.message->bodyText().back(), 'C');
}
