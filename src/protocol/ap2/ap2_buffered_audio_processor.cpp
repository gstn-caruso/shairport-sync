/*
 * AirPlay 2 Buffered Audio Processor. This file is part of Shairport Sync
 * Copyright (c) Mike Brady 2025
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "session/session_state.hpp"
#include "audio/output/audio_player_adapter.hpp"
#include "protocol/ap2/ap2_buffered_audio_processor.h"
#include "protocol/ap2/buffered_block_input.hpp"
#include "runtime/common.h"
#include "playback/player.h"
#include "protocol/rtp/rtp.h"
#include "transport/buffered_tcp_transport.hpp"
#include "platform/utilities/network_utilities.h"
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
import receiver.protocol.ap2.buffered_coordinator;


static void logBufferedFlush(const BufferedFlushPolicy::Decision &decision, ssrc_t ssrc) {
  using Kind = BufferedFlushPolicy::EventKind;
  for (const auto &event : decision.events()) {
    switch (event.kind) {
    case Kind::immediateStarted:
      debug(2, "immediate flush started at sequence number %u until sequence number of %u.",
            event.sequence, event.untilSequence);
      break;
    case Kind::immediateOverrun:
      if (ssrc == SSRC_NONE) {
        debug(2, "immediate flush endpoint followed by a SSRC_NONE packet. Seq_no is %u, "
                 "conn->ap2_immediate_flush_until_sequence_number is %u.",
              event.sequence, event.untilSequence);
      } else {
        debug(1, "immediate flush may have escaped its endpoint! Seq_no is %u, "
                 "conn->ap2_immediate_flush_until_sequence_number is %u.",
              event.sequence, event.untilSequence);
      }
      break;
    case Kind::immediateCompleted:
      debug(2, "immediate flush completed at seq_no: %u, "
               "conn->ap2_immediate_flush_until_sequence_number: %u.",
            event.sequence, event.untilSequence);
      break;
    case Kind::immediateDiscard:
      debug(4, "immediate flush of block %u until block %u", event.sequence, event.untilSequence);
      break;
    case Kind::deferredCancelled:
    case Kind::deferredActivated:
    case Kind::deferredCompleted: {
      const char *prefix = event.kind == Kind::deferredCancelled
          ? "deferred flush cancelled by an immediate flush:  "
          : event.kind == Kind::deferredActivated ? "deferred flush activated:  "
                                                 : "deferred flush terminated: ";
      debug(event.kind == Kind::deferredCancelled ? 1 : 2,
            "%sflushFromTS: %12u, flushFromSeq: %12u, "
            "flushUntilTS: %12u, flushUntilSeq: %12u, timestamp: %12u.",
            prefix, event.fromTimestamp, event.fromSequence,
            event.untilTimestamp, event.untilSequence, event.timestamp);
      break;
    }
    case Kind::deferredOverrun:
      debug(2, "deferred flush terminated due to overshoot at block %u: flushFromTS: %12u, "
               "flushFromSeq: %12u, flushUntilTS: %12u, flushUntilSeq: %12u, timestamp: %12u.",
            event.sequence, event.fromTimestamp, event.fromSequence,
            event.untilTimestamp, event.untilSequence, event.timestamp);
      debug(2, "immediate flush was %s.", event.immediateWasActive ? "on" : "off");
      break;
    case Kind::deferredDiscard:
      debug(4, "deferred flush of block: %u, timestamp: %u, SSRC: \"%s\". flushFromTS: %12u, "
               "flushFromSeq: %12u, flushUntilTS: %12u, flushUntilSeq: %12u, timestamp: %12u.",
            event.sequence, event.timestamp, get_ssrc_name(ssrc), event.fromTimestamp,
            event.fromSequence, event.untilTimestamp, event.untilSequence, event.timestamp);
      break;
    }
  }
}


void rtp_buffered_audio_cleanup_handler(__attribute__((unused)) void *arg) {
  debug(2, "Buffered Audio Receiver Cleanup Start.");
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  safe_socket_close(&conn->buffered_audio_socket);
  debug(3, "Connection %d: closing TCP Buffered Audio port: %u.", conn->connection_number,
        conn->local_buffered_audio_port);
  debug(2, "Connection %d: rtp_buffered_audio_processor exit.", conn->connection_number);
}

class RuntimeBufferedSession : public BufferedSessionPort {
public:
  RuntimeBufferedSession(SessionState &session, double desired) : session_(session), desired_(desired) {}
  bool playbackEnabled() const override { return session_.ap2_play_enabled != 0; }
  bool evaluateFlush(bool everRead, const BufferedPacketMetadata &packet) override {
    bool discard;
    pthread_mutex_lock_and_cleanup_push(&session_.flush_mutex);
    const auto decision = session_.bufferedFlush.evaluate(everRead,packet.sequence,packet.timestamp);
    discard = decision.discardCurrent;
    logBufferedFlush(decision,static_cast<ssrc_t>(packet.ssrc));
    pthread_cleanup_pop(1);
    return discard;
  }
  void observeRead(const BufferedBlockRead &read) override {
    session_.statistics.observeBufferedBytes(read.prefixRemaining);
    if (read.bodyRemaining) session_.statistics.observeBufferedBytes(*read.bodyRemaining);
  }
  std::span<const uint8_t> key() const override {
    return session_.session_key ? std::span<const uint8_t>(session_.session_key,32) : std::span<const uint8_t>{};
  }
  void diagnostic(const BufferedReceiverDiagnostic &event) override {
    std::visit([this](const auto &value) { report(value); },event);
  }
private:
  SessionState &session_;
  double desired_;
  void report(const BufferedPlayDiagnostic &event) {
    debug(2,event.started ? "Play started." : "Play stopped.");
  }
  void report(const BufferedFormatDiagnostic &event) {
    switch (event.kind) {
    case BufferedFormatKind::unknown:
      debug(2,"Unrecognised SSRC: %u.",event.packet.ssrc);
      break;
    case BufferedFormatKind::initial:
      debug(2,"Preparing initial decoding chain for %s.",get_ssrc_name(static_cast<ssrc_t>(event.packet.ssrc)));
      break;
    case BufferedFormatKind::changed:
      debug(2,"Connection %d: incoming audio encoding is%s \"%s\".",session_.connection_number,
            event.switching ? " switching to" : "",get_ssrc_name(static_cast<ssrc_t>(event.packet.ssrc)));
      break;
    }
  }
  void report(const BufferedHistoryDiagnostic &event) {
    if (event.kind == BufferedHistoryKind::sequence)
      debug(2,"reading block %u, the sequence number differs from the expected sequence number %u. "
              "The previous sequence number was %u",event.packet.sequence,event.expected,event.previous);
    else
      debug(2,"reading block %u, the timestamp %u differs from expected_timestamp %u.",
            event.packet.sequence,event.packet.timestamp,event.expected);
  }
  void report(const BufferedReadDiagnostic &event) {
    if (event.read.status == BufferedBlockReadStatus::invalidSize) {
      debug(1,"Connection %d: invalid buffered audio block length %u.",session_.connection_number,event.read.declaredLength);
    } else if (event.read.status == BufferedBlockReadStatus::readError) {
      char errorstring[1024];
      (void)!strerror_r(event.read.errorCode,errorstring,sizeof(errorstring));
      debug(1,"error in rtp_buffered_audio_processor %d: \"%s\". Could not recv a data_len .",
            event.read.errorCode,errorstring);
    } else {
      debug(2,"Connection %d: buffered audio port closed!",session_.connection_number);
    }
  }
  void report(const BufferedAdmissionDiagnostic &event) {
    switch (event.kind) {
    case BufferedAdmissionDiagnosticKind::clockWait:
      debug(4,"just you wait, Henry Higgins, without valid timing information...");
      break;
    case BufferedAdmissionDiagnosticKind::early:
      debug(1,"incoming frame, sequence number %u suddenly has a lead time of %f seconds, "
              "with a desired decoded buffer length of %f.",event.packet.sequence,event.leadNs*1E-9,desired_);
      break;
    case BufferedAdmissionDiagnosticKind::drop:
      debug(1,"dropping buffer that should have played before the last actually played.");
      break;
    case BufferedAdmissionDiagnosticKind::late:
      debug(3,"skipped deciphering block %u with timestamp %u because its lead time is out of range at %f seconds.",
            event.packet.sequence,event.packet.timestamp,event.leadNs*1E-9);
      break;
    case BufferedAdmissionDiagnosticKind::invalidFormat:
      debug(2,"Unrecognised or invalid ssrc: %s.",get_ssrc_name(static_cast<ssrc_t>(event.packet.ssrc)));
      break;
    }
  }
  void report(const BufferedPreparationDiagnostic &event) {
    switch (event.kind) {
    case BufferedPreparationKind::missingKey:
      debug(2,"No session key, so the audio packet can not be deciphered -- skipped.");
      break;
    case BufferedPreparationKind::authenticationFailed:
      debug(1,"Error decrypting audio packet %u -- packet length %zd.",event.packet.sequence,
            static_cast<ssize_t>(event.packet.length));
      break;
    case BufferedPreparationKind::unsupportedRate:
      debug(1,"Unsupported AAC sample rate %u.",event.rate);
      break;
    }
  }
  void report(const BufferedSubmissionDiagnostic &event) {
    const auto &packet = event.packet;
    switch (event.kind) {
    case BufferedSubmissionKind::firstMute:
      debug(2,"Connection %d: muting first AAC block -- block %u -- timestamp %u.",
            session_.connection_number,packet.sequence,packet.timestamp);
      break;
    case BufferedSubmissionKind::first:
      debug(2,"Connection %d: first block %u, first timestamp %u.",session_.connection_number,packet.sequence,packet.timestamp);
      break;
    case BufferedSubmissionKind::discontinuity:
      debug(2,"Connection %d: unexpected timestamp in block %u. Actual: %u, expected: %u difference: %d, %f ms. "
              "Positive means later, i.e. a gap. First timestamp was %u, payload type: \"%s\".",
            session_.connection_number,packet.sequence,packet.timestamp,event.expected,event.gap,
            1000.0*event.gap/event.rate,event.firstTimestamp,get_ssrc_name(static_cast<ssrc_t>(packet.ssrc)));
      break;
    case BufferedSubmissionKind::discontinuityMute:
      debug(2,"Connection %d: muting first AAC block -- block %u -- following a timestamp discontinuity, timestamp %u.",
            session_.connection_number,packet.sequence,packet.timestamp);
      break;
    case BufferedSubmissionKind::skip:
      debug(2,"skipping block %u because it is too old. Timestamp difference: %d, length of block: %zu.",
            packet.sequence,event.gap,static_cast<size_t>(event.frames));
      break;
    case BufferedSubmissionKind::submitted:
      debug(4,"block %u, timestamp %u, length %u sent to the player.",packet.sequence,packet.timestamp,event.frames);
      break;
    }
  }
};
class RuntimeBufferedClock : public BufferedClockPort {
public:
  explicit RuntimeBufferedClock(SessionState &session) : session_(session) {}
  bool ready() override { return have_ptp_timing_information(&session_) != 0; }
  std::optional<uint64_t> schedule(uint32_t timestamp) override {
    uint64_t time = 0;
    if (frame_to_local_time(timestamp,&time,&session_) == 0) return time;
    return std::nullopt;
  }
  uint64_t now() override { return get_absolute_time_in_ns(); }
  void wait(unsigned microseconds) override { usleep(microseconds); }
  void diagnoseAnchor() override {
    uint32_t rtp = 0;
    uint64_t local = 0;
    if (get_ptp_anchor_local_time_info(&session_,&rtp,&local) == clock_ok)
      debug(3,"anchorRTP: %u, anchorLocalTime: %" PRIu64 ".",rtp,local);
    else
      debug(3,"Clock not okay");
  }
private:
  SessionState &session_;
};
class RuntimeBufferedAudioSink : public BufferedAudioSinkPort {
public:
  explicit RuntimeBufferedAudioSink(SessionState &session) : session_(session) {}
  BufferedInputShape shape() const override { return {session_.inputAudio.framesPerPacket(),session_.inputAudio.sampleRate()}; }
  void initialize(uint32_t ssrc) override { prepareIncomingAudio(session_,static_cast<ssrc_t>(ssrc)); }
  void reset() override { reset_buffer(&session_); }
  unsigned submit(const BufferedPacketMetadata &packet,const BufferedAudioSubmission &submission,
                  std::span<uint8_t> payload) override {
    return player_put_packet(packet.ssrc,submission.sequence,packet.timestamp,payload.data(),payload.size(),
                             submission.mute,submission.gap,&session_);
  }
private:
  SessionState &session_;
};

void *rtp_buffered_audio_processor(void *arg) {
  auto *conn = static_cast<rtsp_conn_info *>(arg);
  int previousDecoderCancellationState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousDecoderCancellationState);
  conn->decoder.reset();
  conn->resampler.reset();
  pthread_setcancelstate(previousDecoderCancellationState, nullptr);
  conn->bufferedFlush.resetForBufferedReceiver();

  pthread_cleanup_push(rtp_buffered_audio_cleanup_handler, arg);
  {
    BufferedTcpTransport transport(conn->buffered_audio_socket, conn->ap2_audio_buffer_size,
                                   "ap2_buf_rdr_" + std::to_string(conn->connection_number));
    const auto started = transport.start();
    if (!started) {
      debug(1, "Connection %d: buffered TCP transport startup failed: %d.",
            conn->connection_number, started.error());
    } else {
      const auto desired = config.audio_decoded_buffer_desired_length;
      RuntimeBufferedSession session(*conn,desired);
      RuntimeBufferedClock clock(*conn);
      RuntimeBufferedAudioSink audio(*conn);
      BufferedReceiverCoordinator coordinator(transport,session,clock,audio,desired);
      coordinator.run();
    }
  }
  pthread_cleanup_pop(1);
  pthread_exit(nullptr);
}
