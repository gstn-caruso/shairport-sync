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
  std::size_t failSize = 0;
  bool failArray = false;
  bool failed = false;
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
static void failAllocation(std::size_t size, bool array) {
  if (allocationProbe && !allocationProbe->failed && size == allocationProbe->failSize &&
      array == allocationProbe->failArray) {
    allocationProbe->failed = true;
    throw std::bad_alloc();
  }
}
static void *poisonAllocation(void *storage, std::size_t size) {
  if (allocationProbe && size == allocationProbe->poisonSize) {
    std::memset(storage, 0x5a, size);
    allocationProbe->poisoned = true;
  }
  return storage;
}
extern "C" void *__wrap__Znwm(std::size_t size) {
  failAllocation(size, false);
  return poisonAllocation(__real__Znwm(size), size);
}
extern "C" void *__wrap__Znam(std::size_t size) {
  failAllocation(size, true);
  return poisonAllocation(__real__Znam(size), size);
}

namespace {
class AllocationClock : public RtspRequestClock {
public:
  std::uint64_t nowNs() override { return 0; }
};
class AllocationEffects : public RtspRequestEffects {
public:
  void diagnostic(RtspRequestDiagnostic event, RtspRequestPhase phase, int) override {
    diagnosed = true;
    lastEvent = event;
    lastPhase = phase;
  }
  void closeHeaderChannel() override { ++closes; }
  void stalled() override {}
  bool diagnosed = false;
  RtspRequestDiagnostic lastEvent{};
  RtspRequestPhase lastPhase{};
  unsigned closes = 0;
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
    untouchedTail = allocationProbe && allocationProbe->poisoned && destination.back() == 0x5a;
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

namespace {
struct AllocationFailure {
  std::string name;
  std::size_t size;
  bool array;
  RtspRequestPhase phase;
  bool longPath = false;
};
void PrintTo(const AllocationFailure &scenario, std::ostream *output) { *output << scenario.name; }
class AllocationFailures : public testing::TestWithParam<AllocationFailure> {};
}

TEST_P(AllocationFailures, ActualAllocationFailureReleasesRequestAndAllowsRetry) {
  const auto &scenario = GetParam();
  GrowingBodyInput input;
  if (scenario.longPath)
    input.headers = "POST " + std::string(100, 'P') + " RTSP/1.0\r\nContent-Length: 5000\r\n\r\nAB";
  AllocationClock clock;
  AllocationEffects effects;
  RtspRequestReader reader(input, clock, effects);
  AllocationProbe probe;
  probe.failSize = scenario.size;
  probe.failArray = scenario.array;
  RtspRequestResult result;
  {
    ProbeScope scope(probe);
    result = reader.read();
  }
  ASSERT_TRUE(probe.failed) << "Expected real allocation site was not reached";
  EXPECT_EQ(result.status, RtspRequestStatus::allocationFailure);
  EXPECT_FALSE(result.message);
  EXPECT_TRUE(effects.diagnosed);
  EXPECT_EQ(effects.lastEvent, RtspRequestDiagnostic::allocationFailure);
  EXPECT_EQ(effects.lastPhase, scenario.phase);
  EXPECT_EQ(effects.closes, 0u);
  input.headersRead = false;
  auto retry = reader.read();
  ASSERT_EQ(retry.status, RtspRequestStatus::success);
  ASSERT_TRUE(retry.message);
  EXPECT_EQ(retry.message->bodyLength(), 5000u);
  EXPECT_TRUE(retry.message->bodyStartsWith("AB"));
}

INSTANTIATE_TEST_SUITE_P(OwnedStorage, AllocationFailures, testing::Values(
    AllocationFailure{"Message", sizeof(RtspMessage), false, RtspRequestPhase::headers},
    AllocationFailure{"HeaderBuffer", 4096, true, RtspRequestPhase::headers},
    AllocationFailure{"RequestLine", 101, false, RtspRequestPhase::headers, true},
    AllocationFailure{"HeaderList", sizeof(RtspMessage::Header), false, RtspRequestPhase::headers},
    AllocationFailure{"BodyGrowth", 5000, true, RtspRequestPhase::body},
    AllocationFailure{"BodyCopy", 5001, false, RtspRequestPhase::body}
), [](const auto &scenario) { return scenario.param.name; });
