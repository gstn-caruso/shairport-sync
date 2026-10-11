#include "protocol/rtsp/rtsp_message.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
import receiver.protocol.rtsp.request;

class RequestInput : public RtspRequestInput {
public:
  bool stopped() override { return stop; }
  RtspRequestRead read(std::span<char> destination) override {
    sizes.push_back(destination.size());
    return {0, 0, false};
  }
  bool stop = true;
  std::vector<std::size_t> sizes;
};
class RequestClock : public RtspRequestClock {
public:
  std::uint64_t nowNs() override { return now; }
  std::uint64_t now = 0;
};
class RequestEffects : public RtspRequestEffects {
public:
  void diagnostic(RtspRequestDiagnostic event, RtspRequestPhase phase, int error) override {
    diagnostics.push_back({event, phase, error});
  }
  void closeHeaderChannel() override { ++closes; }
  void stalled() override { ++warnings; }
  struct Diagnostic { RtspRequestDiagnostic event; RtspRequestPhase phase; int error; };
  std::vector<Diagnostic> diagnostics;
  unsigned closes = 0, warnings = 0;
};

TEST(RtspRequestReader, EmptyStoppedInputReturnsNoRequestWithoutReading) {
  RequestInput input;
  RequestClock clock;
  RequestEffects effects;
  RtspRequestReader reader(input, clock, effects);
  auto result = reader.read();
  EXPECT_EQ(result.status, RtspRequestStatus::shutdown);
  EXPECT_FALSE(result.message);
  EXPECT_TRUE(input.sizes.empty());
  EXPECT_EQ(effects.closes, 0u);
}
