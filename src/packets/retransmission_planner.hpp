#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <vector>

namespace shairport::packets {

struct RetryPolicy {
  uint64_t firstCheckAfter = UINT64_MAX, repeatAfter = 0, minimumRemaining = 0, playbackLatency = 0;
};
struct PacketWindow {
  uint16_t first, end;
  uint16_t size() const { return static_cast<uint16_t>(end - first); }
  auto sequences() const {
    return std::views::iota(0u, static_cast<unsigned>(size())) |
           std::views::transform([first = first](unsigned offset) {
             return static_cast<uint16_t>(first + offset);
           });
  }
};
struct ResendRange {
  uint16_t first, count;
  bool extendIfAdjacent(uint16_t sequence) {
    if (static_cast<uint16_t>(first + count) != sequence)
      return false;
    ++count;
    return true;
  }
};

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
  static constexpr std::size_t capacity = 1024;
  std::array<std::optional<Missing>, capacity> missing_;
};

}
