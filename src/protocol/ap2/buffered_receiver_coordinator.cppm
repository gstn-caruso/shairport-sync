module;
#include "transport/exact_byte_input.hpp"
#include "protocol/ap2/buffered_block_input.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <variant>
export module receiver.protocol.ap2.buffered_coordinator;

export {
struct BufferedPacketMetadata { std::uint32_t sequence = 0, timestamp = 0, ssrc = 0; std::size_t length = 0; };
struct BufferedInputShape { unsigned frames = 0, rate = 0; };
struct BufferedAudioSubmission { std::uint16_t sequence; bool mute; std::int32_t gap; };
struct BufferedPlayDiagnostic { bool started; };
enum class BufferedFormatKind { changed, unknown, initial };
struct BufferedFormatDiagnostic { BufferedFormatKind kind; BufferedPacketMetadata packet; bool switching = false; };
enum class BufferedHistoryKind { sequence, timestamp };
struct BufferedHistoryDiagnostic { BufferedHistoryKind kind; BufferedPacketMetadata packet; std::uint32_t expected, previous; };
struct BufferedReadDiagnostic { BufferedBlockRead read; };
enum class BufferedAdmissionDiagnosticKind { clockWait, early, drop, late, invalidFormat };
struct BufferedAdmissionDiagnostic { BufferedAdmissionDiagnosticKind kind; BufferedPacketMetadata packet; std::int64_t leadNs = 0; };
enum class BufferedPreparationKind { missingKey, authenticationFailed, unsupportedRate };
struct BufferedPreparationDiagnostic { BufferedPreparationKind kind; BufferedPacketMetadata packet; unsigned rate = 0; };
enum class BufferedSubmissionKind { firstMute, first, discontinuity, discontinuityMute, skip, submitted };
struct BufferedSubmissionDiagnostic {
  BufferedSubmissionKind kind;
  BufferedPacketMetadata packet;
  std::int32_t gap = 0;
  std::uint32_t expected = 0, firstTimestamp = 0;
  unsigned frames = 0, rate = 0;
};
using BufferedReceiverDiagnostic = std::variant<BufferedPlayDiagnostic,BufferedFormatDiagnostic,
    BufferedHistoryDiagnostic,BufferedReadDiagnostic,BufferedAdmissionDiagnostic,
    BufferedPreparationDiagnostic,BufferedSubmissionDiagnostic>;
class BufferedSessionPort {
public:
  virtual ~BufferedSessionPort() = default;
  virtual bool playbackEnabled() const = 0;
  virtual bool evaluateFlush(bool everRead, const BufferedPacketMetadata &) = 0;
  virtual void observeRead(const BufferedBlockRead &) = 0;
  virtual std::span<const std::uint8_t> key() const = 0;
  virtual void diagnostic(const BufferedReceiverDiagnostic &) = 0;
};
class BufferedClockPort {
public:
  virtual ~BufferedClockPort() = default;
  virtual bool ready() = 0;
  virtual std::optional<std::uint64_t> schedule(std::uint32_t timestamp) = 0;
  virtual std::uint64_t now() = 0;
  virtual void wait(unsigned microseconds) = 0;
  virtual void diagnoseAnchor() = 0;
};
class BufferedAudioSinkPort {
public:
  virtual ~BufferedAudioSinkPort() = default;
  virtual BufferedInputShape shape() const = 0;
  virtual void initialize(std::uint32_t ssrc) = 0;
  virtual void reset() = 0;
  virtual unsigned submit(const BufferedPacketMetadata &, const BufferedAudioSubmission &,
                          std::span<std::uint8_t> payload) = 0;
};
enum class BufferedReceiverResult { continued, closed, readError, invalidSize };
class BufferedReceiverCoordinator {
public:
  BufferedReceiverCoordinator(ExactByteInput &, BufferedSessionPort &, BufferedClockPort &,
                              BufferedAudioSinkPort &, double desiredBufferSeconds);
  ~BufferedReceiverCoordinator();
  BufferedReceiverCoordinator(const BufferedReceiverCoordinator &) = delete;
  BufferedReceiverCoordinator &operator=(const BufferedReceiverCoordinator &) = delete;
  BufferedReceiverResult advance();
  BufferedReceiverResult run();
private:
  struct State;
  std::unique_ptr<State> state_;
};
}
