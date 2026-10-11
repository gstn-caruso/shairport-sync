module;
#include <bit>
#include <cstdint>
#include <optional>
#include <stdexcept>
module receiver.protocol.ap2.buffered_playback;

BufferedPlayAction BufferedPlaybackPolicy::onPlayState(bool enabled) {
  BufferedPlayAction action;
  action.started = !playing_ && enabled;
  action.stopped = playing_ && !enabled;
  action.resetPlayer = action.stopped;
  action.needFreshBlock = action.started;
  if (action.stopped) playedCount_ = 0;
  playing_ = enabled;
  return action;
}
BufferedAdmission BufferedPlaybackPolicy::admit(std::optional<std::uint64_t> scheduledNs,
    std::uint64_t nowNs, unsigned frames, unsigned sampleRate) {
  if (!scheduledNs) return {BufferedAdmissionKind::waitClock, false, 20000};
  const auto lead = std::bit_cast<std::int64_t>(*scheduledNs - nowNs);
  if (playing_ && lead * 1E-9 < desiredBufferSeconds_ + 0.1) {
    earlyWarning_ = false;
    if (playedCount_ != 0 && std::bit_cast<std::int64_t>(*scheduledNs) <= 0)
      return {BufferedAdmissionKind::dropBeforePrevious, false, 0, lead};
    return {lead < 0 ? BufferedAdmissionKind::consumeLate : BufferedAdmissionKind::prepare, false, 0, lead};
  }
  if (sampleRate == 0) throw std::invalid_argument("Buffered packet wait requires a positive sample rate");
  const bool warning = !earlyWarning_ && lead * 1E-9 > desiredBufferSeconds_ + 0.2;
  earlyWarning_ |= warning;
  return {BufferedAdmissionKind::waitPacket, warning, 2 * ((1000000u * frames) / sampleRate), lead};
}
void BufferedPlaybackPolicy::seedPlayerSequence(std::uint32_t sequence) {
  playerSequence_ = sequence & 0xffff;
}
BufferedSubmissionPlan BufferedPlaybackPolicy::planAuthenticated(std::uint32_t timestamp,
    bool isAac, unsigned frames) {
  const bool first = playedCount_ == 0;
  if (first) firstTimestamp_ = timestamp;
  const auto gap = first ? std::int32_t{0} : std::bit_cast<std::int32_t>(timestamp - expectedTimestamp_);
  bool skip = false;
  if (gap < 0) {
    const std::int32_t magnitude = -gap;
    skip = static_cast<unsigned>(magnitude) > frames;
  }
  return {first, isAac && (first || gap != 0), skip, playerSequence_, gap,
          firstTimestamp_, expectedTimestamp_};
}
void BufferedPlaybackPolicy::didSubmit(std::uint32_t timestamp, unsigned returnedFrames) {
  ++playerSequence_;
  expectedTimestamp_ = timestamp + returnedFrames;
  ++playedCount_;
}
