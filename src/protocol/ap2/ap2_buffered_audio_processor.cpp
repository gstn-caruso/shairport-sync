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
#include "runtime/common.h"
#include "playback/player.h"
#include "protocol/rtp/rtp.h"
#include "transport/buffered_tcp_transport.hpp"
#include "platform/utilities/mod23.h"
#include "platform/utilities/network_utilities.h"
#include <sodium.h>
#include <stdint.h>
#include "protocol/ap2/buffered_block_input.hpp"
import receiver.protocol.ap2.buffered_block;
import receiver.protocol.ap2.buffered_playback;


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

void *rtp_buffered_audio_processor(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  // #include <syscall.h>
  // debug(1, "Connection %d: rtp_buffered_audio_processor PID %d start", conn->connection_number,
  //         syscall(SYS_gettid));
  int previousDecoderCancellationState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousDecoderCancellationState);
  conn->decoder.reset();
  conn->resampler.reset();
  pthread_setcancelstate(previousDecoderCancellationState, nullptr);

  conn->bufferedFlush.resetForBufferedReceiver();

  pthread_cleanup_push(rtp_buffered_audio_cleanup_handler, arg);

  {
  BufferedTcpTransport buffered_audio(conn->buffered_audio_socket, conn->ap2_audio_buffer_size,
      "ap2_buf_rdr_" + std::to_string(conn->connection_number));
  const auto transportStarted = buffered_audio.start();
  if (!transportStarted) {
    debug(1, "Connection %d: buffered TCP transport startup failed: %d.",
          conn->connection_number, transportStarted.error());
  } else {

  const size_t buffer_packet_size = 16 * 1024; // it looks as if 4096 is the largest size (?)
  uint8_t *packet = static_cast<uint8_t *>(malloc(buffer_packet_size));
  if (packet == NULL)
    debug(1, "cannot allocate an audio packet buffer of %zu bytes!", buffer_packet_size);
  pthread_cleanup_push(malloc_cleanup, &packet);

  std::optional<BufferedAudioBlock> bufferedBlock;
  ssrc_t payload_ssrc =
      SSRC_NONE; // this is the SSRC of the payload, needed to decide if it should be muted
  ssrc_t previous_ssrc = SSRC_NONE;
  std::optional<AudioFormat> payloadFormat;

  uint32_t seq_no =
      0; // audio packet number. Initialised to avoid a "possibly uninitialised" warning.
  uint32_t previous_seqno = 0;

  uint32_t timestamp = 0; // initialised to avoid a "possibly uninitialised" warning.
  uint32_t previous_timestamp = 0;

  BufferedPlaybackPolicy playback(config.audio_decoded_buffer_desired_length);

  ssize_t nread;

  int new_audio_block_needed = 0; // goes true when a block is needed, false one is read in, but
                                  // will be made true by flushing or by playing the block
  int finished = 0;

  uint64_t blocks_read = 0;


  // wait until our timing information is valid
  while (have_ptp_timing_information(conn) == 0)
    usleep(1000);

  reset_buffer(conn); // in case there is any garbage in the player

  do {

    const auto play = playback.onPlayState(conn->ap2_play_enabled != 0);
    if (play.started) {
      debug(2, "Play started.");
    }
    if (play.needFreshBlock)
      new_audio_block_needed = 1;

    if (play.stopped) {
      debug(2, "Play stopped.");
    }
    if (play.resetPlayer)
      reset_buffer(conn);


    // now, if get_next_block is non-zero, read a block. We may flush or use it

    if (new_audio_block_needed != 0) {
      auto blockRead = readBufferedAudioBlock(buffered_audio, std::span(packet, buffer_packet_size));
      conn->statistics.observeBufferedBytes(blockRead.prefixRemaining);
      if (blockRead.bodyRemaining)
        conn->statistics.observeBufferedBytes(*blockRead.bodyRemaining);
      nread = blockRead.status == BufferedBlockReadStatus::complete ? static_cast<ssize_t>(blockRead.count)
          : blockRead.status == BufferedBlockReadStatus::readError ? -1 : 0;
      if (blockRead.status == BufferedBlockReadStatus::complete) {
        auto parsed = BufferedAudioBlock::parse(std::span(packet, blockRead.count));
        if (!parsed) {
          blockRead.status = BufferedBlockReadStatus::invalidSize;
          nread = 0;
        } else {
          bufferedBlock = *parsed;

          // got the block
          blocks_read++;                  // note, this doesn't mean they are valid audio blocks

          // get the sequence number
          // see https://en.wikipedia.org/wiki/Real-time_Transport_Protocol#Packet_header
          // the Marker bit is always set, and it and the remaining 23 bits form the sequence number

          previous_seqno = seq_no;
          seq_no = bufferedBlock->sequence();

          previous_timestamp = timestamp;
          timestamp = bufferedBlock->timestamp();

          if (payload_ssrc != SSRC_NONE)
            previous_ssrc = payload_ssrc;
          payload_ssrc = static_cast<ssrc_t>(bufferedBlock->ssrc());
          payloadFormat = AudioFormat::fromSsrc(payload_ssrc);

          if ((payload_ssrc != previous_ssrc) && (payload_ssrc != SSRC_NONE)) {
            if (!payloadFormat) {
              debug(2, "Unrecognised SSRC: %u.", payload_ssrc);
            } else {
              debug(2, "Connection %d: incoming audio encoding is%s \"%s\".",
                    conn->connection_number, previous_ssrc == SSRC_NONE ? "" : " switching to",
                    get_ssrc_name(payload_ssrc));
            }
          }

          if (payloadFormat) {
            new_audio_block_needed = 0; // a valid block has been read.
            // if necessary, set the input rate...
            if (conn->inputAudio.sampleRate() == 0) {
              debug(2, "Preparing initial decoding chain for %s.", get_ssrc_name(payload_ssrc));
              prepareIncomingAudio(*conn, payload_ssrc);
              playback.seedPlayerSequence(seq_no);
            } else {
              uint32_t t_expected_seqno = (previous_seqno + 1) & 0x7fffff;
              if (t_expected_seqno != seq_no) {
                debug(2,
                      "reading block %u, the sequence number differs from the expected sequence "
                      "number %u. The previous sequence number was %u",
                      seq_no, t_expected_seqno, previous_seqno);
              }
              auto previousFormat = AudioFormat::fromSsrc(previous_ssrc);
              uint32_t t_expected_timestamp = previous_timestamp +
                  (previousFormat ? previousFormat->framesPerPacket() : 0);
              int32_t diff = timestamp - t_expected_timestamp;
              if (diff != 0) {
                debug(2, "reading block %u, the timestamp %u differs from expected_timestamp %u.",
                      seq_no, timestamp, t_expected_timestamp);
              }
            }
          }
        }
      }

      if (blockRead.status == BufferedBlockReadStatus::invalidSize) {
        debug(1, "Connection %d: invalid buffered audio block length %u.",
              conn->connection_number, blockRead.declaredLength);
        finished = 1;
      } else if (nread == 0) {
        // nread is 0 -- the port has been closed
        debug(2, "Connection %d: buffered audio port closed!", conn->connection_number);
        finished = 1;
      } else if (nread < 0) {
        char errorstring[1024];
        (void)!strerror_r(blockRead.errorCode, (char *)errorstring,
                          sizeof(errorstring)); // (void) ! to suppress unused response warning
        debug(1, "error in rtp_buffered_audio_processor %d: \"%s\". Could not recv a data_len .",
              blockRead.errorCode, errorstring);
        finished = 1;
      }
    }

    if (finished == 0) {
      pthread_mutex_lock_and_cleanup_push(&conn->flush_mutex);
      const auto flush = conn->bufferedFlush.evaluate(blocks_read != 0, seq_no, timestamp);
      if (flush.discardCurrent)
        new_audio_block_needed = 1;
      logBufferedFlush(flush, payload_ssrc);
      pthread_cleanup_pop(1); // the mutex

      if (new_audio_block_needed == 0) {
        uint64_t scheduledTime = 0;
        const bool validTime = frame_to_local_time(timestamp, &scheduledTime, conn) == 0;
        const auto admission = playback.admit(
            validTime ? std::optional<uint64_t>{scheduledTime} : std::nullopt,
            validTime ? get_absolute_time_in_ns() : 0,
            conn->inputAudio.framesPerPacket(), conn->inputAudio.sampleRate());
        if (admission.kind == BufferedAdmissionKind::waitClock) {
          debug(4, "just you wait, Henry Higgins, without valid timing information...");
          usleep(admission.waitUs);
        } else if (admission.kind == BufferedAdmissionKind::waitPacket) {
          if (admission.warnEarly)
            debug(1, "incoming frame, sequence number %u suddenly has a lead time of %f seconds, "
                     "with a desired decoded buffer length of %f.",
                  seq_no, admission.leadNs * 1E-9, config.audio_decoded_buffer_desired_length);
          usleep(admission.waitUs);
        } else {
          if (admission.kind == BufferedAdmissionKind::dropBeforePrevious) {
            debug(1, "dropping buffer that should have played before the last actually played.");
          } else if (payloadFormat && bufferedBlock) {
            if (admission.kind == BufferedAdmissionKind::prepare) {
              const auto key = conn->session_key
                  ? std::span<const uint8_t>(conn->session_key, 32) : std::span<const uint8_t>{};
              auto prepared = bufferedBlock->prepare(
                  {payloadFormat->isAac() ? BufferedBlockCodec::aac : BufferedBlockCodec::alac,
                   payloadFormat->aacChannelConfiguration()},
                  key, conn->inputAudio.sampleRate());
              if (!prepared) {
                if (prepared.error() == BufferedBlockError::missingKey)
                  debug(2, "No session key, so the audio packet can not be deciphered -- skipped.");
                else if (prepared.error() == BufferedBlockError::authenticationFailed)
                  debug(1, "Error decrypting audio packet %u -- packet length %zd.", seq_no, nread);
              } else {
                if (payloadFormat->isAac() && conn->inputAudio.sampleRate() != 44100 &&
                    conn->inputAudio.sampleRate() != 48000)
                  debug(1, "Unsupported AAC sample rate %u.", conn->inputAudio.sampleRate());
                const auto submission = playback.planAuthenticated(
                    timestamp, payloadFormat->isAac(), payloadFormat->framesPerPacket());
                if (submission.first) {
                  if (submission.mute)
                    debug(2, "Connection %d: muting first AAC block -- block %u -- timestamp %u.",
                          conn->connection_number, seq_no, timestamp);
                  debug(2, "Connection %d: first block %u, first timestamp %u.",
                        conn->connection_number, seq_no, timestamp);
                } else if (submission.gap != 0) {
                  debug(2, "Connection %d: unexpected timestamp in block %u. Actual: %u, expected: %u "
                           "difference: %d, %f ms. Positive means later, i.e. a gap. "
                           "First timestamp was %u, payload type: \"%s\".",
                        conn->connection_number, seq_no, timestamp, submission.expectedTimestamp,
                        submission.gap, 1000.0 * submission.gap / conn->inputAudio.sampleRate(),
                        submission.firstTimestamp, get_ssrc_name(payload_ssrc));
                  if (submission.mute)
                    debug(2, "Connection %d: muting first AAC block -- block %u -- following a "
                             "timestamp discontinuity, timestamp %u.",
                          conn->connection_number, seq_no, timestamp);
                }
                if (submission.skipTooOld) {
                  debug(2, "skipping block %u because it is too old. Timestamp difference: %d, "
                           "length of block: %zu.",
                        seq_no, submission.gap, static_cast<size_t>(payloadFormat->framesPerPacket()));
                } else {
                  const auto packetSize = player_put_packet(
                      payload_ssrc, submission.sequence, timestamp, prepared->data(), prepared->size(),
                      submission.mute, submission.gap, conn);
                  debug(4, "block %u, timestamp %u, length %u sent to the player.",
                        seq_no, timestamp, packetSize);
                  playback.didSubmit(timestamp, packetSize);
                }
              }
            } else {
              debug(3, "skipped deciphering block %u with timestamp %u because its lead time is "
                       "out of range at %f seconds.",
                    seq_no, timestamp, admission.leadNs * 1E-9);
              uint32_t anchorRtp = 0;
              uint64_t anchorLocalTime = 0;
              if (get_ptp_anchor_local_time_info(conn, &anchorRtp, &anchorLocalTime) == clock_ok)
                debug(3, "anchorRTP: %u, anchorLocalTime: %" PRIu64 ".", anchorRtp, anchorLocalTime);
              else
                debug(3, "Clock not okay");
            }
          } else {
            debug(2, "Unrecognised or invalid ssrc: %s.", get_ssrc_name(payload_ssrc));
          }
          new_audio_block_needed = 1;
        }
      }
    }
  } while (finished == 0);
  // debug(1, "Connection %d: rtp_buffered_audio_processor PID %d exiting", conn->connection_number,
  //       syscall(SYS_gettid));
  pthread_cleanup_pop(1); // packet
  }
  }
  pthread_cleanup_pop(1); // do the cleanup.
  // debug(1, "Connection %d: rtp_buffered_audio_processor PID %d finish", conn->connection_number,
  //       syscall(SYS_gettid));
  pthread_exit(NULL);
}
