#include "retransmission_planner.hpp"

bool RetransmissionPlanner::Missing::isDue(uint64_t now, RetryPolicy policy) const {
  if (now < noticed)
    return false;
  const auto age = now - noticed;
  if (age < policy.firstCheckAfter || age > policy.playbackLatency ||
      policy.playbackLatency - age < policy.minimumRemaining)
    return false;
  return attempts == 0 || (now >= lastRequest && now - lastRequest >= policy.repeatAfter);
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
  for (unsigned offset = 0; offset < window.size(); ++offset) {
    const auto sequence = static_cast<uint16_t>(window.first + offset);
    auto &missing = missing_[sequence % capacity];
    if (!missing || missing->sequence != sequence || !missing->isDue(now, policy))
      continue;
    if (!ranges.empty() && static_cast<uint16_t>(ranges.back().first + ranges.back().count) == sequence)
      ++ranges.back().count;
    else
      ranges.push_back({sequence, 1});
    missing->lastRequest = now;
    ++missing->attempts;
  }
  return ranges;
}
void RetransmissionPlanner::reset() {
  for (auto &missing : missing_)
    missing.reset();
}
