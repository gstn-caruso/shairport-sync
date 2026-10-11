module;
#include "transport/exact_byte_input.hpp"
#include "protocol/ap2/buffered_block_input.hpp"
#include <memory>
#include <optional>
module receiver.protocol.ap2.buffered_coordinator;
import receiver.protocol.ap2.buffered_playback;

struct BufferedReceiverCoordinator::State {
  ExactByteInput &input;
  BufferedSessionPort &session;
  BufferedClockPort &clock;
  BufferedAudioSinkPort &sink;
  BufferedPlaybackPolicy playback;
  bool initialized = false;
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
    session.evaluateFlush(false, {});
    const auto scheduled = clock.schedule(0);
    const auto shape = sink.shape();
    const auto admission = playback.admit(scheduled, scheduled ? clock.now() : 0, shape.frames, shape.rate);
    if (admission.kind == BufferedAdmissionKind::waitClock || admission.kind == BufferedAdmissionKind::waitPacket)
      clock.wait(admission.waitUs);
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
