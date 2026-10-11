module;
#include "transport/exact_byte_input.hpp"
#include "protocol/ap2/buffered_block_input.hpp"
#include <memory>
#include <optional>
#include <array>
#include <cstdint>
#include "audio/format/audio_format.hpp"
module receiver.protocol.ap2.buffered_coordinator;
import receiver.protocol.ap2.buffered_playback;
import receiver.protocol.ap2.buffered_block;

struct BufferedReceiverCoordinator::State {
  ExactByteInput &input;
  BufferedSessionPort &session;
  BufferedClockPort &clock;
  BufferedAudioSinkPort &sink;
  BufferedPlaybackPolicy playback;
  bool initialized = false;
  bool needsRead = false;
  std::array<std::uint8_t,16384> wire;
  std::optional<BufferedAudioBlock> block;
  std::optional<AudioFormat> format;
  BufferedPacketMetadata packet{};
  std::uint64_t blocksRead = 0;
  std::uint32_t previousSsrc = 0;
  State(ExactByteInput &bytes, BufferedSessionPort &live, BufferedClockPort &time,
        BufferedAudioSinkPort &audio, double desired)
      : input(bytes), session(live), clock(time), sink(audio), playback(desired) {}
  BufferedReceiverResult advance() {
    if (!initialized) {
      while (!clock.ready()) clock.wait(1000);
      sink.reset();
      initialized = true;
    }
    const auto play = playback.onPlayState(session.playbackEnabled());
    if (play.started || play.stopped) session.diagnostic(BufferedPlayDiagnostic{play.started});
    if (play.resetPlayer) sink.reset();
    if (play.needFreshBlock) needsRead = true;
    if (needsRead) {
      auto result = readBlock();
      if (result != BufferedReceiverResult::continued) return result;
    }
    if (session.evaluateFlush(blocksRead != 0, packet)) needsRead = true;
    if (needsRead) return BufferedReceiverResult::continued;
    const auto scheduled = clock.schedule(packet.timestamp);
    const auto shape = sink.shape();
    const auto admission = playback.admit(scheduled, scheduled ? clock.now() : 0, shape.frames, shape.rate);
    if (admission.kind == BufferedAdmissionKind::waitClock || admission.kind == BufferedAdmissionKind::waitPacket) {
      if (admission.kind == BufferedAdmissionKind::waitClock)
        session.diagnostic(BufferedAdmissionDiagnostic{BufferedAdmissionDiagnosticKind::clockWait,packet});
      if (admission.warnEarly)
        session.diagnostic(BufferedAdmissionDiagnostic{BufferedAdmissionDiagnosticKind::early,packet,admission.leadNs});
      clock.wait(admission.waitUs);
    } else {
      if (admission.kind == BufferedAdmissionKind::dropBeforePrevious) {
        session.diagnostic(BufferedAdmissionDiagnostic{BufferedAdmissionDiagnosticKind::drop,packet});
      } else if (block && format) {
        if (admission.kind == BufferedAdmissionKind::prepare) prepareCurrent(shape);
        else {
          session.diagnostic(BufferedAdmissionDiagnostic{BufferedAdmissionDiagnosticKind::late,packet,admission.leadNs});
          clock.diagnoseAnchor();
        }
      } else {
        session.diagnostic(BufferedAdmissionDiagnostic{BufferedAdmissionDiagnosticKind::invalidFormat,packet});
      }
      needsRead = true;
    }
    return BufferedReceiverResult::continued;
  }
  void reportSubmission(BufferedSubmissionKind kind, const BufferedSubmissionPlan &plan,
                        unsigned frames, unsigned rate) {
    session.diagnostic(BufferedSubmissionDiagnostic{kind,packet,plan.gap,plan.expectedTimestamp,
                                                   plan.firstTimestamp,frames,rate});
  }
  void prepareCurrent(BufferedInputShape shape) {
    auto prepared = block->prepare({format->isAac() ? BufferedBlockCodec::aac : BufferedBlockCodec::alac,
                                   format->aacChannelConfiguration()}, session.key(), shape.rate);
    if (!prepared) {
      if (prepared.error() == BufferedBlockError::missingKey)
        session.diagnostic(BufferedPreparationDiagnostic{BufferedPreparationKind::missingKey,packet});
      else if (prepared.error() == BufferedBlockError::authenticationFailed)
        session.diagnostic(BufferedPreparationDiagnostic{BufferedPreparationKind::authenticationFailed,packet});
      return;
    }
    if (format->isAac() && shape.rate != 44100 && shape.rate != 48000)
      session.diagnostic(BufferedPreparationDiagnostic{BufferedPreparationKind::unsupportedRate,packet,shape.rate});
    const auto plan = playback.planAuthenticated(packet.timestamp,format->isAac(),format->framesPerPacket());
    if (plan.first) {
      if (plan.mute) reportSubmission(BufferedSubmissionKind::firstMute,plan,0,shape.rate);
      reportSubmission(BufferedSubmissionKind::first,plan,0,shape.rate);
    } else if (plan.gap != 0) {
      reportSubmission(BufferedSubmissionKind::discontinuity,plan,0,shape.rate);
      if (plan.mute) reportSubmission(BufferedSubmissionKind::discontinuityMute,plan,0,shape.rate);
    }
    if (plan.skipTooOld) {
      reportSubmission(BufferedSubmissionKind::skip,plan,format->framesPerPacket(),shape.rate);
      return;
    }
    const auto returned = sink.submit(packet,{plan.sequence,plan.mute,plan.gap},*prepared);
    reportSubmission(BufferedSubmissionKind::submitted,plan,returned,shape.rate);
    playback.didSubmit(packet.timestamp,returned);
  }
  BufferedReceiverResult readBlock() {
    const auto read = readBufferedAudioBlock(input,wire);
    session.observeRead(read);
    if (read.status != BufferedBlockReadStatus::complete) {
      session.diagnostic(BufferedReadDiagnostic{read});
      if (read.status == BufferedBlockReadStatus::invalidSize) return BufferedReceiverResult::invalidSize;
      if (read.status == BufferedBlockReadStatus::readError) return BufferedReceiverResult::readError;
      return BufferedReceiverResult::closed;
    }
    auto parsed = BufferedAudioBlock::parse(std::span(wire).first(read.count));
    if (!parsed) {
      auto invalid = read;
      invalid.status = BufferedBlockReadStatus::invalidSize;
      session.diagnostic(BufferedReadDiagnostic{invalid});
      return BufferedReceiverResult::invalidSize;
    }
    const auto previous = packet;
    if (packet.ssrc != SSRC_NONE) previousSsrc = packet.ssrc;
    block = *parsed;
    packet = {block->sequence(),block->timestamp(),block->ssrc(),read.count};
    ++blocksRead;
    format = AudioFormat::fromSsrc(static_cast<ssrc_t>(packet.ssrc));
    if (packet.ssrc != previousSsrc && packet.ssrc != SSRC_NONE)
      session.diagnostic(BufferedFormatDiagnostic{format ? BufferedFormatKind::changed : BufferedFormatKind::unknown,
                                                 packet, previousSsrc != SSRC_NONE});
    if (format) {
      needsRead = false;
      if (sink.shape().rate == 0) {
        session.diagnostic(BufferedFormatDiagnostic{BufferedFormatKind::initial,packet});
        sink.initialize(packet.ssrc);
        playback.seedPlayerSequence(packet.sequence);
      } else {
        const auto expectedSequence = (previous.sequence + 1) & 0x7fffff;
        if (expectedSequence != packet.sequence)
          session.diagnostic(BufferedHistoryDiagnostic{BufferedHistoryKind::sequence,packet,
                                                      expectedSequence,previous.sequence});
        const auto oldFormat = AudioFormat::fromSsrc(static_cast<ssrc_t>(previousSsrc));
        const auto expectedTimestamp = previous.timestamp + (oldFormat ? oldFormat->framesPerPacket() : 0);
        if (packet.timestamp != expectedTimestamp)
          session.diagnostic(BufferedHistoryDiagnostic{BufferedHistoryKind::timestamp,packet,
                                                      expectedTimestamp,previous.timestamp});
      }
    }
    return BufferedReceiverResult::continued;
  }
};
BufferedReceiverCoordinator::BufferedReceiverCoordinator(ExactByteInput &input, BufferedSessionPort &session,
    BufferedClockPort &clock, BufferedAudioSinkPort &sink, double desired)
    : state_(std::make_unique<State>(input,session,clock,sink,desired)) {}
BufferedReceiverCoordinator::~BufferedReceiverCoordinator() = default;
BufferedReceiverResult BufferedReceiverCoordinator::advance() { return state_->advance(); }
BufferedReceiverResult BufferedReceiverCoordinator::run() {
  BufferedReceiverResult result;
  do { result = advance(); } while (result == BufferedReceiverResult::continued);
  return result;
}
