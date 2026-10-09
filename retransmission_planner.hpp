#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

struct RetryPolicy {
  uint64_t firstCheckAfter = UINT64_MAX, repeatAfter = 0, minimumRemaining = 0, playbackLatency = 0;
};
struct PacketWindow {
  uint16_t first, end;
  uint16_t size() const { return static_cast<uint16_t>(end - first); }
};
struct ResendRange { uint16_t first, count; };

class RetransmissionPlanner {
public:
  void noteMissing(uint16_t sequence, uint64_t now);
  void resolve(uint16_t sequence);
  std::vector<ResendRange> due(uint64_t now, RetryPolicy, PacketWindow);
  void reset();

private:
  struct Missing {
    uint16_t sequence;
    uint64_t noticed, lastRequest = 0;
    unsigned attempts = 0;
    bool requestIfDue(uint64_t now, RetryPolicy policy);
  };
  static constexpr size_t capacity = 1024;
  std::array<std::optional<Missing>, capacity> missing_;
};
