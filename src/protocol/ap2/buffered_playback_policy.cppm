module;
#include <cstdint>
#include <optional>
export module receiver.protocol.ap2.buffered_playback;

export struct BufferedPlayAction {
  bool started = false, stopped = false, resetPlayer = false, needFreshBlock = false;
};
export enum class BufferedAdmissionKind { waitClock, waitPacket, prepare, consumeLate, dropBeforePrevious };
export struct BufferedAdmission {
  BufferedAdmissionKind kind;
  bool warnEarly = false;
  unsigned waitUs = 0;
  std::int64_t leadNs = 0;
};
export struct BufferedSubmissionPlan {
  bool first = false, mute = false, skipTooOld = false;
  std::uint16_t sequence = 0;
  std::int32_t gap = 0;
  std::uint32_t firstTimestamp = 0, expectedTimestamp = 0;
};
export class BufferedPlaybackPolicy {
public:
  explicit BufferedPlaybackPolicy(double desiredBufferSeconds) : desiredBufferSeconds_(desiredBufferSeconds) {}
  BufferedPlayAction onPlayState(bool enabled);
  // Packet waits require a positive sample rate; absent timing does not require a packet shape.
  BufferedAdmission admit(std::optional<std::uint64_t> scheduledNs, std::uint64_t nowNs,
                          unsigned frames, unsigned sampleRate);
  void seedPlayerSequence(std::uint32_t sequence);
  BufferedSubmissionPlan planAuthenticated(std::uint32_t timestamp, bool isAac, unsigned frames);
  void didSubmit(std::uint32_t timestamp, unsigned returnedFrames);
private:
  double desiredBufferSeconds_;
  bool playing_ = false;
  bool earlyWarning_ = false;
  unsigned playedCount_ = 0;
  std::uint16_t playerSequence_ = 0;
  std::uint32_t expectedTimestamp_ = 0, firstTimestamp_ = 0;
};
