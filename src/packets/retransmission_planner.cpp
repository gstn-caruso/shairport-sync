#include "packets/retransmission_planner.hpp"

namespace shairport::packets {

bool RetransmissionPlanner::Missing::requestIfDue(uint64_t now, RetryPolicy policy) {
  if (now < noticed)
    return false;
  const auto age = now - noticed;
  if (age < policy.firstCheckAfter || age > policy.playbackLatency ||
      policy.playbackLatency - age < policy.minimumRemaining)
    return false;
  if (attempts != 0 && (now < lastRequest || now - lastRequest < policy.repeatAfter))
    return false;
  lastRequest = now;
  ++attempts;
  return true;
}
void RetransmissionPlanner::noteMissing(uint16_t sequence, uint64_t now) {
  missing_[sequence % capacity] = Missing{sequence, now};
}
void RetransmissionPlanner::resolve(uint16_t sequence) {
  auto &missing = missing_[sequence % capacity];
  if (missing && missing->sequence == sequence)
    missing.reset();
}
std::vector<ResendRange> RetransmissionPlanner::due(uint64_t now, RetryPolicy policy,
                                                 PacketWindow window) {
  std::vector<ResendRange> ranges;
  if (window.size() > capacity)
    return ranges;
  ranges.reserve(window.size());
  for (auto sequence : window.sequences()) {
    auto &missing = missing_[sequence % capacity];
    if (!missing || missing->sequence != sequence || !missing->requestIfDue(now, policy))
      continue;
    if (ranges.empty() || !ranges.back().extendIfAdjacent(sequence))
      ranges.push_back({sequence, 1});
  }
  return ranges;
}
void RetransmissionPlanner::reset() {
  for (auto &missing : missing_)
    missing.reset();
}

}
