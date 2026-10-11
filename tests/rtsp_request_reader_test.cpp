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
    if (index < fragments.size()) {
      const auto &fragment = fragments[index];
      const auto copied = std::min(fragment.size() - offset, destination.size());
      std::copy_n(fragment.data() + offset, copied, destination.data());
      offset += copied;
      if (offset == fragment.size()) { ++index; offset = 0; }
      return {static_cast<std::ptrdiff_t>(copied), 0, false};
    }
    return {0, 0, false};
  }
  bool stop = true;
  std::vector<std::size_t> sizes;
  std::vector<std::string> fragments;
  std::size_t index = 0, offset = 0;
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

TEST(RtspRequestReader, FragmentedHeadersAndBodyReadOnlyMissingBytes) {
  RequestInput input;
  input.stop = false;
  input.fragments = {"POST /feed", "back RTSP/1.0\r\nContent-Length: 4\r\n\r\nA", "B", std::string("\0D", 2)};
  RequestClock clock;
  RequestEffects effects;
  RtspRequestReader reader(input, clock, effects);
  auto result = reader.read();
  ASSERT_EQ(result.status, RtspRequestStatus::success);
  ASSERT_TRUE(result.message);
  EXPECT_TRUE(result.message->requestsPath("/feedback"));
  EXPECT_EQ(result.message->bodyText(), std::string_view("AB\0D", 4));
  EXPECT_EQ(input.sizes, (std::vector<std::size_t>{4096, 4086, 3, 2}));
  EXPECT_EQ(effects.closes, 0u);
  EXPECT_EQ(effects.warnings, 0u);
}
