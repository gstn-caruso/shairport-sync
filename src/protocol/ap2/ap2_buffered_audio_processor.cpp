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
  uint16_t sequence_number_for_player = 0;

  uint32_t timestamp = 0; // initialised to avoid a "possibly uninitialised" warning.
  uint32_t previous_timestamp = 0;

  uint32_t expected_timestamp = 0;
  uint64_t previous_buffer_should_be_time = 0;

  ssize_t nread;

  int new_audio_block_needed = 0; // goes true when a block is needed, false one is read in, but
                                  // will be made true by flushing or by playing the block
  int finished = 0;

  uint64_t blocks_read = 0;

  uint32_t first_timestamp_in_this_sequence = 0;
  int packets_played_in_this_sequence = 0;

  int play_enabled = 0;

  int very_early_packets_signalled = 0;
  // double requested_lead_time = 0.0; // normal lead time minimum -- maybe  it should be about 0.1

  // wait until our timing information is valid
  while (have_ptp_timing_information(conn) == 0)
    usleep(1000);

  reset_buffer(conn); // in case there is any garbage in the player

  do {

    if ((play_enabled == 0) && (conn->ap2_play_enabled != 0)) {
      // play newly started
      debug(2, "Play started.");
      new_audio_block_needed = 1;
    }

    if ((play_enabled != 0) && (conn->ap2_play_enabled == 0)) {
      debug(2, "Play stopped.");
      packets_played_in_this_sequence = 0; // not all blocks read are played...
      reset_buffer(conn); // stop play ASAP
    }

    play_enabled = conn->ap2_play_enabled;

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
              sequence_number_for_player =
                  seq_no & 0xffff; // this is arbitrary -- the sequence_number_for_player numbers will
                                   // be sequential irrespective of seq_no jumps...
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

      // now, if the block is not invalidated by the flush code, see if we need
      // to decode it and pass it to the player
      if (new_audio_block_needed == 0) {
        // is there space in the player thread's buffer system?
        // size_t player_buffer_occupancy = get_audio_buffer_occupancy(conn);
        // debug(1,"player buffer size and occupancy: %u and %u", player_buffer_size,
        // player_buffer_occupancy);

        // If we are playing and there is room in the player buffer, and the block it not too
        // early, go ahead and decode the block
        // and send it to the player. Otherwise, keep the block and sleep for a while.

        // calculate if there is room in the decoded audio buffer...

        // debug(1, "frames buffered: %f seconds, desired length: %f seconds.", (1.0 *
        // player_buffer_occupancy * conn->frames_per_packet) / conn->input_rate,
        // config.audio_decoded_buffer_desired_length);

        // int audio_decoded_buffer_below_desired_length = ((1.0 * player_buffer_occupancy *
        // conn->frames_per_packet) / conn->input_rate) <=
        // config.audio_decoded_buffer_desired_length;
        uint64_t buffer_should_be_time;

        int have_valid_time = (frame_to_local_time(timestamp, &buffer_should_be_time, conn) == 0);

        // A slight problem here is that counting the number of buffers may not be sufficient,
        // because the actual device may be
        // taking data in large quantities at a single time.

        // So we just have to ensure that there
        // is enough of a lead time maintained for sufficient audio to be available to prevent
        // the device from under-running.

        // If means that the Shairport Sync player might run out of audio occasionally, but
        // as long as the device has enough in its buffer, everything is fine.

        // But it also means that Shairport Sync's buffers must be sufficient to hold all the
        // entire lead-time's amount of audio in case the device has a zero-sized buffer.

        if (have_valid_time != 0) {
          // calculate the lead time to make sure it's not too early...
          int64_t lead_time = buffer_should_be_time - get_absolute_time_in_ns();
          if ((play_enabled != 0) &&
              (lead_time * 1E-9 < (config.audio_decoded_buffer_desired_length + 0.1))) {
            //            && (audio_decoded_buffer_below_desired_length != 0)
            very_early_packets_signalled = 0; // reset very early packet warning signaller

            // try to identify blocks that are timed to before the last buffer, and drop 'em
            int64_t time_from_last_buffer_time =
                buffer_should_be_time - previous_buffer_should_be_time;

            if ((packets_played_in_this_sequence == 0) || (time_from_last_buffer_time > 0)) {

              if (payloadFormat && bufferedBlock) {
                if (lead_time >= 0) {
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
                  }
                  if (prepared) {
                    auto *payload_pointer = prepared->data();
                    const auto payload_length = prepared->size();
                    if (payloadFormat->isAac() && conn->inputAudio.sampleRate() != 44100 &&
                        conn->inputAudio.sampleRate() != 48000)
                      debug(1, "Unsupported AAC sample rate %u.", conn->inputAudio.sampleRate());
                    int mute =
                        ((packets_played_in_this_sequence == 0) && payloadFormat->isAac());
                    if (mute) {
                      debug(2, "Connection %d: muting first AAC block -- block %u -- timestamp %u.",
                            conn->connection_number, seq_no, timestamp);
                    }
                    int32_t timestamp_difference = 0;
                    if (packets_played_in_this_sequence == 0) {
                      // first_block_in_this_sequence = seq_no;
                      first_timestamp_in_this_sequence = timestamp;
                      debug(2,
                            "Connection %d: "
                            "first block %u, first timestamp %u.",
                            conn->connection_number, seq_no, timestamp);
                    } else {
                      timestamp_difference = timestamp - expected_timestamp;
                      if (timestamp_difference != 0) {
                        debug(2,
                              "Connection %d: "
                              "unexpected timestamp in block %u. Actual: %u, expected: %u "
                              "difference: %d, "
                              "%f ms. "
                              "Positive means later, i.e. a gap. First timestamp was %u, payload "
                              "type: \"%s\".",
                              conn->connection_number, seq_no, timestamp, expected_timestamp,
                              timestamp_difference,
                              1000.0 * timestamp_difference / conn->inputAudio.sampleRate(),
                              first_timestamp_in_this_sequence, get_ssrc_name(payload_ssrc));
                        // mute the first packet after a discontinuity
                        if (payloadFormat->isAac()) {
                          debug(2,
                                "Connection %d: muting first AAC block -- block %u -- following a "
                                "timestamp discontinuity, timestamp %u.",
                                conn->connection_number, seq_no, timestamp);
                          mute = 1;
                        }
                      }
                    }
                    int skip_this_block = 0;
                    if (timestamp_difference < 0) {

                      // uncomment this to drop incoming new buffers that are too old and for whose
                      // timings buffers have already been decoded and placed in the player queue
                      // this is easier, but maybe the new late buffers are better than the previous
                      // ones
                      // (?)

                      int32_t abs_timestamp_difference = -timestamp_difference;
                      if ((size_t)abs_timestamp_difference > payloadFormat->framesPerPacket()) {
                        skip_this_block = 1;
                        debug(2,
                              "skipping block %u because it is too old. Timestamp "
                              "difference: %d, length of block: %zu.",
                              seq_no, timestamp_difference,
                              static_cast<size_t>(payloadFormat->framesPerPacket()));
                      }
                    }
                    if (skip_this_block == 0) {
                      uint32_t packet_size = player_put_packet(
                          payload_ssrc, sequence_number_for_player, timestamp, payload_pointer,
                          payload_length, mute, timestamp_difference, conn);
                      debug(4, "block %u, timestamp %u, length %u sent to the player.", seq_no,
                            timestamp, packet_size);
                      sequence_number_for_player++;                 // simply increment
                      expected_timestamp = timestamp + packet_size; // for the next time
                      packets_played_in_this_sequence++;
                    }
                  }
                } else {
                  debug(3,
                        "skipped deciphering block %u with timestamp %u because its lead time is "
                        "out of range at %f "
                        "seconds.",
                        seq_no, timestamp, lead_time * 1.0E-9);
                  uint32_t currentAnchorRTP = 0;
                  uint64_t currentAnchorLocalTime = 0;
                  if (get_ptp_anchor_local_time_info(conn, &currentAnchorRTP,
                                                     &currentAnchorLocalTime) == clock_ok) {
                    debug(3, "anchorRTP: %u, anchorLocalTime: %" PRIu64 ".", currentAnchorRTP,
                          currentAnchorLocalTime);
                  } else {
                    debug(3, "Clock not okay");
                  }
                }
              } else {
                debug(2, "Unrecognised or invalid ssrc: %s.", get_ssrc_name(payload_ssrc));
              }
            } else {
              debug(1, "dropping buffer that should have played before the last actually played.");
            }
            new_audio_block_needed = 1; // the block has been used up and is no longer current
          } else {
            if ((very_early_packets_signalled == 0) &&
                (lead_time * 1E-9 > (config.audio_decoded_buffer_desired_length + 0.2))) {
              debug(1,
                    "incoming frame, sequence number %u suddenly has a lead time of %f seconds, "
                    "with a desired "
                    "decoded buffer length of %f.",
                    seq_no, 1.0 * lead_time * 1E-9, config.audio_decoded_buffer_desired_length);
              very_early_packets_signalled = 1;
            }
            usleep(((1000000 * conn->inputAudio.framesPerPacket()) / conn->inputAudio.sampleRate()) *
                   2); // wait for approximately the length of two packets
          }
        } else {
          debug(4, "just you wait, Henry Higgins, without valid timing information...");
          usleep(20000); // just you wait, Henry Higgins...
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
