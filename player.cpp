/*
 * Slave-clocked ALAC stream player. This file is part of Shairport.
 * Copyright (c) James Laird 2011, 2013
 * All rights reserved.
 *
 * Modifications for audio synchronisation, AirPlay 2
 * and related work, copyright (c) Mike Brady 2014--2026
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

#include "session_state.hpp"
#include "audio_format.hpp"
#include "audio_player_adapter.hpp"
#include "statistics_formatter.hpp"
#include "volume_runtime.hpp"
#include <algorithm>
#include <bit>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/types.h>
#include <unistd.h>

#include "config.h"









#include "common.h"
#include "mdns.h"
#include "player.h"
#include "rtp.h"
#include "rtsp.h"



#include "ptp-utilities.h"

#include <libavutil/version.h>


#include "activity_monitor.h"


void do_flush(uint32_t timestamp, rtsp_conn_info *conn);

size_t avflush(rtsp_conn_info *conn);

void ab_resync(rtsp_conn_info *conn) {
  conn->packetBuffer.reset();
  conn->last_seqno_valid = 0;
  conn->ab_buffering = 1;
}

void reset_input_flow_metrics(rtsp_conn_info *conn) {
  conn->statistics.resetInputEpoch();
  conn->initial_reference_time = 0;
  conn->initial_reference_timestamp = 0;
}



static void free_audio_buffers(rtsp_conn_info *conn) { conn->packetBuffer.reset(); }

void reset_buffer(rtsp_conn_info *conn) {
  ab_resync(conn);
  avflush(conn);
  if (config.output->flush)
    config.output->flush();
}

size_t get_audio_buffer_occupancy(rtsp_conn_info *conn) {
  return conn->packetBuffer.occupancy();
}

const char *get_category_string(airplay_stream_c cat) {
  const char *category;
  switch (cat) {
  case unspecified_stream_category:
    category = "unspecified stream";
    break;
  case ptp_stream:
    category = "PTP stream";
    break;
  case remote_control_stream:
    category = "Remote Control stream";
    break;
  default:
    category = "Unexpected stream code";
    break;
  }
  return category;
}


void clear_decoding_chain(rtsp_conn_info *conn) {
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  conn->decoder.reset();
  pthread_setcancelstate(previousState, nullptr);
}

void clear_software_resampler(rtsp_conn_info *conn) {
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  conn->resampler.reset();
  pthread_setcancelstate(previousState, nullptr);
}

int ssrc_is_recognised(ssrc_t ssrc) { return AudioFormat::fromSsrc(ssrc).has_value(); }

int ssrc_is_aac(ssrc_t ssrc) {
  auto format = AudioFormat::fromSsrc(ssrc);
  return format && format->isAac();
}

const char *get_ssrc_name(ssrc_t ssrc) {
  if (auto format = AudioFormat::fromSsrc(ssrc))
    return format->name().data();
  if (ssrc == SSRC_NONE)
    return "None (0)";
  thread_local char unknown[64];
  snprintf(unknown, sizeof(unknown), "<unknown ssrc> (0x%" PRIx32 ")", ssrc);
  return unknown;
}

size_t get_ssrc_block_length(ssrc_t ssrc) {
  auto format = AudioFormat::fromSsrc(ssrc);
  return format ? format->framesPerPacket() : 0;
}

static int setupSoftwareResampler(rtsp_conn_info *conn, ssrc_t ssrc,
                                  AVSampleFormat decodedFormat) {
  auto format = AudioFormat::fromSsrc(ssrc);
  if (!format)
    return 0;
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  uint32_t encoded = CHANNELS_TO_ENCODED_FORMAT(2) | RATE_TO_ENCODED_FORMAT(48000) |
                     FORMAT_TO_ENCODED_FORMAT(SPS_FORMAT_S32_LE);
  if (config.output->get_configuration)
    encoded = config.output->get_configuration(format->channels(), format->sampleRate(),
                                               format->suggestedSampleFormat());
  if (encoded != 0) {
    char *deviceMap = nullptr;
    if (config.output->configure)
      config.output->configure(encoded, &deviceMap);
    std::unique_ptr<char, decltype(&free)> ownedDeviceMap(deviceMap, &free);
    OutputFormat output{RATE_FROM_ENCODED_FORMAT(encoded), CHANNELS_FROM_ENCODED_FORMAT(encoded)};
    output.inputLayout = format->channels() == 6 ? config.six_channel_layout :
                         format->channels() == 8 ? config.eight_channel_layout : AV_CH_LAYOUT_STEREO;
    output.mixdown = config.mixdown_enable != 0;
    output.mixdownLayout = config.mixdown_channel_layout;
    output.mapping.enabled = config.output_channel_mapping_enable != 0;
    for (unsigned index = 0; index < config.output_channel_map_size; ++index)
      output.mapping.names.emplace_back(config.output_channel_map[index]);
    if (ownedDeviceMap)
      output.mapping.deviceNames = ownedDeviceMap.get();
    const auto decoded = decodedFormat != AV_SAMPLE_FMT_NONE ? decodedFormat :
        format->isAac() ? AV_SAMPLE_FMT_FLTP :
        conn->decoder.decodedSampleFormat().value_or(AV_SAMPLE_FMT_FLTP);
    auto configured = conn->resampler.configure(*format, decoded, std::move(output));
    if (configured) {
      if (config.current_output_configuration != encoded)
        debug(2, "Connection %d: outgoing audio switching to: %s.", conn->connection_number,
              short_format_description(encoded));
      config.current_output_configuration = encoded;
      conn->input_rate = format->sampleRate();
      conn->frames_per_packet = format->framesPerPacket();
      const auto shape = conn->resampler.outputShape();
      if (!conn->pcmEncoder.configure(
              {FORMAT_FROM_ENCODED_FORMAT(encoded), shape.channels()}, shape.effectiveBits()))
        die("Unsupported PCM output format.");
    } else {
      debug(1, "Could not configure resampler: %d.", configured.error().nativeCode);
    }
  } else {
    debug(1, "Error setting the configuration of the output backend.");
  }
  pthread_setcancelstate(previousState, nullptr);
  return 0;
}

int setup_software_resampler(rtsp_conn_info *conn, ssrc_t ssrc) {
  return setupSoftwareResampler(conn, ssrc, AV_SAMPLE_FMT_NONE);
}
void prepareIncomingAudio(SessionState &session, ssrc_t ssrc) {
  auto format = AudioFormat::fromSsrc(ssrc);
  if (!format)
    return;
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  auto previousFormat = session.decoder.currentFormat();
  auto prepared = session.decoder.prepare(*format);
  if (!prepared)
    die("Connection %d: could not prepare decoder: %d.", session.connection_number,
        prepared.error().nativeCode);
  else if (*prepared == Preparation::changed) {
    if (config.statistics_requested && previousFormat)
      inform("Connection %d: Incoming Audio Encoding is switching to: \"%s\".",
             session.connection_number, format->name().data());
    session.input_rate = format->sampleRate();
    session.frames_per_packet = format->framesPerPacket();
    session.input_format_is_valid = 1;
  }
  pthread_setcancelstate(previousState, nullptr);
}

void prepare_decoding_chain(rtsp_conn_info *conn, ssrc_t ssrc) {
  prepareIncomingAudio(*conn, ssrc);
}

// take an AV Frame, run it through the swr resampler and map the output to the
// appropriate channels for the output device

// returns the length of time in nanoseconds associated with the frames that are being retained

ConvertedAudio convertIncomingAudio(SessionState &session, const AVFrame &frame) {
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  auto converted = session.resampler.convert(frame);
  if (!converted)
    debug(1, "Could not convert audio frame: %d.", converted.error().nativeCode);
  pthread_setcancelstate(previousState, nullptr);
  return converted ? std::move(*converted) : ConvertedAudio{};
}

OwnedAudioFrame decodeIncomingAudio(SessionState &session, std::span<const uint8_t> bytes) {
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  auto decoded = session.decoder.decode(bytes);
  if (!decoded && decoded.error().kind != DecoderFailure::Kind::packetTooShort)
    debug(1, "error %d during decoding. Data size: %zu", decoded.error().nativeCode, bytes.size());
  pthread_setcancelstate(previousState, nullptr);
  return decoded ? std::move(*decoded) : OwnedAudioFrame{};
}

AVFrame *block_to_avframe(rtsp_conn_info *conn, uint8_t *data, size_t length) {
  return decodeIncomingAudio(*conn, {data, length}).release();
}

static const char *incomingAudioName(const SessionState &session) {
  auto format = session.decoder.currentFormat();
  return format ? format->name().data() : "None (0)";
}

size_t avflush(rtsp_conn_info *conn) {
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  auto flushed = conn->resampler.flush();
  if (!flushed)
    debug(1, "Could not reset resampler: %d.", flushed.error().nativeCode);
  pthread_setcancelstate(previousState, nullptr);
  return flushed.value_or(0);
}


// Thanks to
// https://stackoverflow.com/questions/27558625/how-do-i-use-aes-cbc-encrypt-128-openssl-properly-in-ubuntu
// for inspiration. Changed to a 128-bit key and no padding.



uint32_t player_put_packet(uint32_t ssrc, seq_t seqno, uint32_t timestamp, uint8_t *data,
                           size_t len, int mute, int32_t timestamp_gap, rtsp_conn_info *conn) {
  const auto format = AudioFormat::fromSsrc(static_cast<ssrc_t>(ssrc));
  if (!format)
    return 0;
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  if (!conn->statistics.hasArrivals())
    conn->packetBuffer.reset();
  const uint64_t now = get_absolute_time_in_ns();
  RetryPolicy policy{
      static_cast<uint64_t>(config.resend_control_first_check_time * 1000000000),
      static_cast<uint64_t>(config.resend_control_check_interval_time * 1000000000),
      static_cast<uint64_t>((config.resend_control_last_check_time +
                            config.audio_backend_buffer_desired_length) * 1000000000),
      conn->input_rate ? uint64_t(conn->latency) * 1000000000 / conn->input_rate : 0};
  auto admission = conn->packetBuffer.accept(seqno, now, [&] {
    prepareIncomingAudio(*conn, format->ssrc());
    auto packet = QueuedAudioPacket::decoded(*format, seqno, timestamp, timestamp_gap,
                                             decodeIncomingAudio(*conn, {data, len}));
    if (mute)
      packet.mute();
    return packet;
  }, policy);
  if (admission.kind == ArrivalKind::first || admission.kind == ArrivalKind::overflow) {
    conn->first_packet_timestamp = 0;
    if (admission.kind == ArrivalKind::overflow) {
      conn->ab_buffering = 1;
      conn->last_seqno_valid = 0;
    }
  }
  conn->statistics.recordArrival(now, timestamp, admission.kind);
  pthread_setcancelstate(previousState, nullptr);
  for (const auto range : admission.resendRanges) {
    if (!config.disable_resend_requests) {
      rtp_request_resend(range.first, range.count, conn);
      conn->statistics.recordResendRequested();
    }
  }
  return admission.samples;
}

int32_t rand_in_range(int32_t exclusive_range_limit) {
  static uint32_t lcg_prev = 12345;
  // returns a pseudo random integer in the range 0 to (exclusive_range_limit-1) inclusive
  int64_t sp = lcg_prev;
  int64_t rl = exclusive_range_limit;
  lcg_prev = lcg_prev * 69069 + 3; // crappy psrg
  sp = sp * rl; // 64 bit calculation. Interesting part is above the 32 rightmost bits;
  return sp >> 32;
}

static std::optional<QueuedAudioPacket> buffer_get_frame(rtsp_conn_info *conn,
                                                        int resync_requested) {
  AudioPacketMetadata snapshot{};
  AudioPacketMetadata *curframe = nullptr;
  std::optional<AudioPacketBuffer::Front> front;
  std::optional<QueuedAudioPacket> result;
  int notified_buffer_empty = 0;
  int wait;
  long dac_delay = 0; // long because alsa returns a long

  int output_device_has_been_primed =
      0; // set to true when we have sent at least one silent frame to the DAC

  do {
    const auto observedRevision = conn->packetBuffer.revision();
    curframe = nullptr;
    // debug(3, "buffer_get_frame is iterating");
    // we must have timing information before we can do anything here
    if ((have_timestamp_timing_information(conn)) && (conn->input_format_is_valid != 0)) {

      if (config.output->is_running && config.output->is_running() != 0)
        conn->packetBuffer.requestFlush(0);
      const auto flushed = conn->packetBuffer.applyFlush();
      if (flushed.flushOutput) {
        avflush(conn);
        if (config.output->flush)
          config.output->flush();
      }
      if (flushed.resetTiming) {
        conn->last_seqno_valid = 0;
        conn->ab_buffering = 1;
        conn->first_packet_timestamp = 0;
        conn->first_packet_time_to_play = 0;
        conn->time_since_play_started = 0;
        output_device_has_been_primed = 0;
        dac_delay = 0;
      }

      uint32_t should_be_frame;
      local_time_to_frame(get_absolute_time_in_ns(), &should_be_frame, conn);
      const auto discarded = conn->packetBuffer.discardPacketsStartingBefore(should_be_frame);
      if (discarded)
        conn->last_seqno_valid = 0;
      front = conn->packetBuffer.front();
      if (front) {
        snapshot = front->packet;
        curframe = &snapshot;
        if (resync_requested != 0) {
          debug(2, "Connection %d: reset first_packet_timestamp resync_requested.",
                conn->connection_number);
          conn->ab_buffering = 1;
          conn->first_packet_timestamp = 0;
          conn->first_packet_time_to_play = 0;
          output_device_has_been_primed = 1; // so that it can rely on the delay provided by it
        }

        if (front.has_value()) {

          if (curframe != NULL) {
            uint64_t should_be_time;
            frame_to_local_time(curframe->timestamp, &should_be_time, conn);
            int64_t time_difference = should_be_time - get_absolute_time_in_ns();
            debug(4, "Check packet from buffer %u, timestamp %u, %f seconds ahead.", snapshot.sequence,
                  curframe->timestamp, 0.000000001 * time_difference);
          } else {
            debug(3, "Check packet from buffer %u, empty.", snapshot.sequence);
          }

          if ((curframe) && (curframe->ready)) {
            notified_buffer_empty = 0; // at least one buffer now -- diagnostic only.
            if (conn->ab_buffering) {  // if we are getting packets but not yet forwarding them to
                                       // the player
              if (conn->first_packet_timestamp == 0) { // if this is the very first packet
                conn->first_packet_timestamp =
                    curframe->timestamp; // we will keep buffering until we are
                                         // supposed to start playing this
                debug(2, "Connection %d: first packet timestamp is %u.", conn->connection_number,
                      conn->first_packet_timestamp);

                // Even though it'll be some time before the first frame will be output
                // (and thus some time before the resampling chain is needed),
                // we need to set up the output device to correspond to
                // the input format w.r.t. rate, depth and channels
                // because we'll be sending silence before the first real frame.
                // Set up the output chain, including the software resampler.
                debug(2, "set up the output chain to %s for FFmpeg.",
                      get_ssrc_name(curframe->encoding));
                setupSoftwareResampler(conn, curframe->encoding, front->sampleFormat);
                uint64_t should_be_time;
                frame_to_local_time(conn->first_packet_timestamp, // this will go modulo 2^32
                                    &should_be_time, conn);

                conn->first_packet_time_to_play = should_be_time;

                int64_t lt = conn->first_packet_time_to_play - get_absolute_time_in_ns();

                // can't be too late because we skipped late packets already, FLW.
                debug(2, "Connection %d: lead time for first frame %u: %f seconds.",
                      conn->connection_number, conn->first_packet_timestamp, lt * 0.000000001);
              }

              if (conn->first_packet_time_to_play != 0) {
                // Now that we know the timing of the first packet...
                if (config.output->delay) {
                  // and that the output device is capable of synchronization...

                  // We may send packets of
                  // silence from now until the time the first audio packet should be sent
                  // and then we will send the first packet, which will be followed by
                  // the subsequent packets.
                  // here, we figure out whether and what silence to send.

                  uint64_t should_be_time;

                  // readjust first packet time to play
                  frame_to_local_time(conn->first_packet_timestamp, &should_be_time, conn);

                  int64_t change_in_should_be_time =
                      (int64_t)(should_be_time - conn->first_packet_time_to_play);

                  if (fabs(0.000001 * change_in_should_be_time) >
                      0.001) // ignore this unless if's more than a microsecond
                    debug(
                        2,
                        "Change in estimated first_packet_time: %f milliseconds for first_packet.",
                        0.000001 * change_in_should_be_time);

                  conn->first_packet_time_to_play = should_be_time;

                  int64_t lead_time = conn->first_packet_time_to_play -
                                      get_absolute_time_in_ns(); // negative means late
                  if (lead_time < 0) {
                    debug(2, "Gone past starting time for %u by %" PRId64 " nanoseconds.",
                          conn->first_packet_timestamp, -lead_time);
                    conn->ab_buffering = 0;
                  } else {
                    // do some calculations
                    if ((config.audio_backend_silent_lead_in_time_auto == 1) ||
                        (lead_time <= (int64_t)(config.audio_backend_silent_lead_in_time *
                                                (int64_t)1000000000))) {
                      debug(3, "Lead time: %" PRId64 " nanoseconds.", lead_time);
                      int resp = 0;
                      dac_delay = 0;
                      if (output_device_has_been_primed != 0)
                        resp = config.output->delay(
                            &dac_delay); // we know the output device must have a delay function
                      if (resp == 0) {
                        int64_t gross_frame_gap =
                            ((conn->first_packet_time_to_play - get_absolute_time_in_ns()) *
                             RATE_FROM_ENCODED_FORMAT(config.current_output_configuration)) /
                            1000000000;
                        int64_t exact_frame_gap = gross_frame_gap - dac_delay;

                        debug(4,
                              "Exact frame gap: %" PRId64 ". DAC delay: %ld. Total: %" PRId64
                              ". First packet timestamp: %u. ",
                              exact_frame_gap, dac_delay, gross_frame_gap,
                              conn->first_packet_timestamp);
                        // int64_t frames_needed_to_maintain_desired_buffer =
                        //     (int64_t)(config.audio_backend_buffer_desired_length *
                        //               config.current_output_configuration->rate) -
                        //     dac_delay;
                        // below, remember that exact_frame_gap and
                        // frames_needed_to_maintain_desired_buffer could both be negative
                        int64_t fs =
                            (RATE_FROM_ENCODED_FORMAT(config.current_output_configuration) * 100) /
                            1000; // 100 milliseconds
                        // if there isn't enough time to have the desired buffer size
                        // if (exact_frame_gap <= fs) {
                        //  fs = conn->frames_per_packet * 2;
                        // }
                        // if we are close to the end of buffering,
                        // just add the remaining silence needed and end buffering
                        if (exact_frame_gap < fs) {
                          debug(3, "exact frame below fs of %" PRId64 " frames.", fs);
                          fs = exact_frame_gap;
                          conn->ab_buffering = 0;
                        }
                        if (fs > 0) {
                          auto silence = conn->pcmEncoder.silence(fs);
                          config.output->play(silence.bytes().data(), silence.frames(),
                                              play_samples_are_untimed, 0, 0);
                          output_device_has_been_primed = 1;
                        }
                      } else {
                        if ((resp == -EAGAIN) || (resp == -EIO) || (resp == -ENODEV)) {
                          debug(2, "delay() information not (yet, hopefully!) available.");
                        } else {
                          debug(1, "delay() error %d: \"%s\".", -resp, strerror(-resp));
                        }
                        if (resp == sps_extra_code_output_stalled) {
                          if (config.unfixable_error_reported == 0) {
                            config.unfixable_error_reported = 1;
                            if (config.cmd_unfixable) {
                              command_execute(config.cmd_unfixable, "output_device_stalled", 1);
                            } else {
                              die("an unrecoverable error, \"output_device_stalled\", has been "
                                  "detected.");
                            }
                          }
                        } else {
                          debug(3, "Unexpected response to getting dac delay: %d.", resp);
                        }
                      }
                    }
                  }
                } else {
                  // if the output device doesn't have a delay, we simply send the lead-in
                  int64_t lead_time = conn->first_packet_time_to_play -
                                      get_absolute_time_in_ns(); // negative if we are late
                  int64_t frame_gap =
                      (lead_time * RATE_FROM_ENCODED_FORMAT(config.current_output_configuration)) /
                      1000000000;
                  // debug(1,"%d frames needed.",frame_gap);
                  while (frame_gap > 0) {
                    int64_t fs = RATE_FROM_ENCODED_FORMAT(config.current_output_configuration) / 10;

                    if (fs > frame_gap)
                      fs = frame_gap;

                    auto silence = conn->pcmEncoder.silence(fs);
                    config.output->play(silence.bytes().data(), silence.frames(),
                                        play_samples_are_untimed, 0, 0);
                    frame_gap -= fs;
                  }
                  conn->ab_buffering = 0;
                }
              }
            }
          }
        }
      } else {
        // if (front.has_value())
        // debug(1, "no buffers available at seqno %u.", snapshot.sequence);
      }

      // Here, we work out whether to release a packet or wait
      // We release a packet when the time is right.

      // To work out when the time is right, we need to take account of (1) the actual time the
      // packet should be released, (2) the latency requested, (3) the audio backend latency offset
      // and (4) the desired length of the audio backend's buffer

      // The time is right if the current time is later or the same as
      // The packet time + (latency + latency offset - backend_buffer_length).
      // Note: the last three items are expressed in frames and must be converted to time.

      int do_wait = 0; // don't wait unless we can really prove we must
      if ((front.has_value()) && (curframe) && (curframe->ready) && (curframe->timestamp)) {
        do_wait = 1; // if the current frame exists and is ready, then wait unless it's time to let
                     // it go...

        // here, get the time to play the current frame.

        if (have_timestamp_timing_information(conn)) { // if we have a reference time

          uint64_t time_to_play;

          // we must enable packets to be released early enough for the
          // audio buffer to be filled to the desired length

          uint32_t desired_buffer_latency =
              (uint32_t)(config.audio_backend_buffer_desired_length * conn->input_rate);
          frame_to_local_time(curframe->timestamp - desired_buffer_latency, &time_to_play, conn);
          uint64_t current_buffer_delay = 0;
          int resp = -1;
          if (config.output->delay) {
            long l_delay;
            resp = config.output->delay(&l_delay);
            if (resp == 0) { // no error
              if (l_delay >= 0)
                current_buffer_delay = l_delay;
              else {
                debug(2, "Underrun of %ld frames reported, but ignored.", l_delay);
                current_buffer_delay =
                    0; // could get a negative value if there was underrun, but ignore it.
              }
            }
          }
          // If it's the first packet, or we don't have a working delay() function in the backend,
          // then wait until it's time to play it
          if ((((conn->first_packet_timestamp == curframe->timestamp) || (resp != 0)) &&
               (get_absolute_time_in_ns() >= time_to_play)) ||
              // Otherwise, if it isn't the first packet and we have a valid delay from the backend,
              // ensure the buffer stays nearly full
              ((conn->first_packet_timestamp != curframe->timestamp) && (resp == 0) &&
               (current_buffer_delay < desired_buffer_latency))) {
            do_wait = 0;
          }
          // here, do a sanity check. if the time_to_play is not within a few seconds of the
          // time now, the frame is probably not meant to be there, so let it go.
          if (do_wait != 0) {
            // this is a hack.
            // we subtract two 2^n unsigned numbers and get a signed 2^n result.
            // If we think of the calculation as occurring in modulo 2^n arithmetic
            // then the signed result's magnitude represents the shorter distance around
            // the modulo wheel of values from one number to the other.
            // The sign indicates the direction: positive means clockwise (upwards) from the
            // second number to the first (i.e. the first number comes "after" the second).

            int64_t time_difference = get_absolute_time_in_ns() - time_to_play;
            if ((time_difference > 10000000000) || (time_difference < -10000000000)) {
              debug(2,
                    "crazy time interval of %f seconds between time now: 0x%" PRIx64
                    " and time of packet: %" PRIx64 ".",
                    0.000000001 * time_difference, get_absolute_time_in_ns(), time_to_play);
              debug(2, "packet rtptime: %u, reference_timestamp: %u", curframe->timestamp,
                    conn->clock.referenceFrame());

              do_wait = 0; // let it go
            }
          }
        }
      }
      if (do_wait == 0)
        // wait if the buffer is empty
        if (!front) { // the buffer is empty!
          if (notified_buffer_empty == 0) {
            debug(4, "Connection %d: Buffer Empty", conn->connection_number);
            notified_buffer_empty = 1;
            // reset_input_flow_metrics(conn); // don't do a full flush parameters reset
            // conn->initial_reference_time = 0;
            // conn->initial_reference_timestamp = 0;
            // conn->first_packet_timestamp = 0; // make sure the first packet isn't late
          }
          do_wait = 1;
        }
      wait = (conn->ab_buffering || (do_wait != 0) || (!front.has_value()));
    } else {
      wait = 1; // keep waiting until the timing information becomes available
    }
    if (wait) {
      uint64_t time_to_wait_for_wakeup_ns = 10000000; // default
      if (conn->input_format_is_valid != 0) {
        time_to_wait_for_wakeup_ns =
            1000000000 / conn->input_rate; // this is time period of one frame
        time_to_wait_for_wakeup_ns *=
            4 * conn->frames_per_packet; // about 4 * 7 mS for 352 frames per second
      }

      uint64_t time_of_wakeup_ns = get_realtime_in_ns() + time_to_wait_for_wakeup_ns;
      uint64_t sec = time_of_wakeup_ns / 1000000000;
      uint64_t nsec = time_of_wakeup_ns % 1000000000;

      struct timespec time_of_wakeup;
      time_of_wakeup.tv_sec = sec;
      time_of_wakeup.tv_nsec = nsec;
      // debug(1, "wait for up to %f mS or for the next packet...", time_to_wait_for_wakeup_ns *
      // 1E-6);
      int rc = conn->packetBuffer.waitForChange(observedRevision, time_of_wakeup); // this is a pthread cancellation point
      if ((rc != 0) && (rc != ETIMEDOUT))
        // if (rc)
        debug(3, "pthread_cond_timedwait returned error code %d.", rc);
      // debug(1, "waited");
    }
    if (!wait && front) {
      auto extracted = conn->packetBuffer.takeFrontIf(front->revision);
      if (!extracted) {
        wait = 1;
      } else if (auto packet = std::get_if<QueuedAudioPacket>(&*extracted)) {
        result = std::move(*packet);
      } else {
        conn->statistics.recordMissingPlayback();
        auto format = conn->decoder.currentFormat();
        if (format) {
          result = QueuedAudioPacket::decoded(*format,
              std::get<MissingAudioPacket>(*extracted).sequence, 0, 0, {});
          result->mute();
        } else
          wait = 1;
      }
    }
  } while (wait);

  if (!result)
    return {};
  const auto metadata = result->metadata();
  const auto nativeFormat = result->sampleFormatForConversion();
  if (!conn->resampler.configuredFor(result->format()))
    setupSoftwareResampler(conn, metadata.encoding, nativeFormat);
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  const auto converted = result->convertWith(conn->resampler);
  pthread_setcancelstate(previousState, nullptr);
  if (!converted)
    debug(1, "Could not convert queued audio packet: %d.", converted.error().nativeCode);
  if (conn->last_seqno_valid && uint16_t(conn->last_seqno_read + 1) != metadata.sequence)
    debug(1, "Player: packets out of sequence: expected %u, got %u.",
          uint16_t(conn->last_seqno_read + 1), metadata.sequence);
  conn->last_seqno_valid = 1;
  conn->last_seqno_read = metadata.sequence;
  return result;
}

double suggested_volume(rtsp_conn_info *conn) {
  return conn ? conn->volumeControl.suggestedLevel(sharedVolumeLevel) : sharedVolumeLevel.current();
}

void player_thread_cleanup_handler(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  // debug(1, "Connection %d: player_thread_cleanup_handler start.", conn->connection_number);

  if (config.output->stop) {
    if ((config.decoder_in_use == 1 << decoder_ffmpeg_alac) && (avflush(conn) > 1))
      debug(1, "ffmpeg flush at stop!");
    debug(2, "Connection %d: player: stop the output backend.", conn->connection_number);
    config.output->stop();
  }

  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  debug(3, "Connection %d: player thread main loop exit via player_thread_cleanup_handler.",
        conn->connection_number);

  const auto summary = conn->statistics.sessionSummary(get_absolute_time_in_ns());
  if (summary.hasObservedFrame && config.statistics_requested) {
    StatisticsFormatter formatter({StatisticsStream::realtime, false, false, false});
    inform("%s", formatter.session(conn->connection_number, summary).c_str());
  }




    debug(2, "Cancelling AP2 timing, control and audio threads...");
    if (conn->airplay_stream_type == realtime_stream) {
      debug(2, "Connection %d: Delete Realtime Audio Stream thread", conn->connection_number);
      pthread_cancel(conn->rtp_realtime_audio_thread);
      pthread_join(conn->rtp_realtime_audio_thread, NULL);

    } else if (conn->airplay_stream_type == buffered_stream) {

      debug(3,
            "Connection %d: Delete Buffered Audio Stream thread by player_thread_cleanup_handler",
            conn->connection_number);
      pthread_cancel(conn->rtp_buffered_audio_thread);
      pthread_join(conn->rtp_buffered_audio_thread, NULL);
      debug(3,
            "Connection %d: Deleted Buffered Audio Stream thread by player_thread_cleanup_handler",
            conn->connection_number);
    } else {
      die("Unrecognised Stream Type");
    }

    debug(2, "Connection %d: Delete AirPlay 2 Control thread", conn->connection_number);
    pthread_cancel(conn->rtp_ap2_control_thread);
    pthread_join(conn->rtp_ap2_control_thread, NULL);

  ptp_send_control_message_string("E");

  if (config.decoder_in_use == 1 << decoder_ffmpeg_alac) {
    // debug(1, "FFmpeg clearup");
    clear_software_resampler(conn);
    conn->decoder.reset();
    // debug(1, "FFmpeg clearup done");
  }


  free_audio_buffers(conn);

  conn->rtp_running = 0;

  pthread_setcancelstate(oldState, NULL);
  debug(2, "Connection %d: player terminated.", conn->connection_number);
}

static PlaybackMode playbackModeFor(playback_mode_type mode) {
  switch (mode) {
  case ST_stereo: return PlaybackMode::stereo;
  case ST_mono: return PlaybackMode::mono;
  case ST_reverse_stereo: return PlaybackMode::reverse;
  case ST_left_only: return PlaybackMode::left;
  case ST_right_only: return PlaybackMode::right;
  }
  return PlaybackMode::stereo;
}

static PcmVolumeSnapshot beginPcmFrame(rtsp_conn_info *conn) {
  const auto volume = conn->volumeControl.pcmSnapshot();
  conn->pcmEncoder.beginFrame(volume.gainFixed16, config.playback_mode == ST_mono);
  return volume;
}

void *player_thread_func(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  // if (config.output->prepare)
  // config.output->prepare(); // give the backend its first chance to prepare itself, knowing it
  // has access to the output device (i.e. knowing that it should not be in use by another program
  // at this time).


  conn->latency_warning_issued =
      0; // be permitted to generate a warning each time a play is attempted
  conn->statistics.resetForPlay();
  conn->pcmEncoder.reset();
  conn->playbackSync.resetForPlay();
  conn->ab_buffering = 1;
  conn->first_packet_timestamp = 0;
  conn->flush_output_flushed = 0; // only send a flush command to the output device once
  conn->volumeControl.resetGainForPlay();
  conn->frames_per_packet = 352; // for ALAC -- will be changed if necessary

  conn->ap2_rate = 0;
  conn->ap2_play_enabled = 0;

  unsigned int f = 0;
  for (f = 0; f < MAX_DEFERRED_FLUSH_REQUESTS; f++) {
    conn->ap2_deferred_flush_requests[f].inUse = 0;
    conn->ap2_deferred_flush_requests[f].active = 0;
  }

  // This must be after init_alac_decoder
  ab_resync(conn);


  // conn->connection_state_to_output = get_requested_connection_state_to_output();

  int play_samples = 0;
  uint64_t current_delay;
  bool statisticsHeaderPrinted = false;

  // I think it's useful to keep this prime to prevent it from falling into a pattern with some
  // other process.

  static char rnstate[256];
  initstate(time(NULL), rnstate, 256);

  // signed short *inbuf;
  int inbuflength;

  // remember, the output device may never have been initialised prior to this call
  if (avflush(conn) > 1)
    debug(1, "ffmpeg flush at start!");

  // leave this relic -- jack and soundio still use it
  if (config.output->start != NULL)
    config.output->start(44100, SPS_FORMAT_S16_LE);

  conn->first_packet_timestamp = 0;
  pthread_cleanup_push(player_thread_cleanup_handler, arg); // undo what's been done so far

  // stop looking elsewhere for DACP stuff
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);


  pthread_setcancelstate(oldState, NULL);

  // if not already set, set the volume to the pending_airplay_volume, if any, or otherwise to the
  // suggested volume.

  double initial_volume = suggested_volume(conn);
  debug(2, "Set initial volume to %.6f.", initial_volume);
  player_volume(initial_volume, conn); // will contain a cancellation point if asked to wait

  debug(2, "Play begin");


  // uint32_t flush_to_frame;
  // int enable_flush_to_frame = 0;
  int request_resync = 0;      // will be set if a big discontinuity is detected
  // debug(1, "player begin processing packets");
  while (1) {


    pthread_testcancel(); // allow a pthread_cancel request to take effect.

    beginPcmFrame(conn);

    auto inframe = buffer_get_frame(
        conn, request_resync); // this has a guaranteed [and needed!] cancellation point
    request_resync = 0;
    if (inframe) {
      const auto pcmVolume = beginPcmFrame(conn);
      const auto playback = inframe->metadata();
      if (!inframe->audioBytes().empty()) {
        /*
        {
          uint64_t the_time_this_frame_should_be_played;
                    frame_to_local_time(playback.timestamp,
                                        &the_time_this_frame_should_be_played, conn);
          int64_t lead_time = the_time_this_frame_should_be_played - get_absolute_time_in_ns();
          debug(1, "get_packet %u, lead time is %3.f ms.", playback.timestamp, lead_time *
        0.000001);
        }
        */
        int frames_played = 0;
        int64_t sync_error = 0;
        int amount_to_stuff = 0;
        if (!inframe->audioBytes().empty()) {
          const auto attempt = conn->statistics.recordPlaybackAttempt(get_absolute_time_in_ns());
          const auto play_number = attempt.playNumber;

          if (playback.timestamp == 0) {
            debug(2,
"Player has supplied a silent frame, (possibly frame %u) for play number %" PRIu64 ", "
                  "status 0x%X after %u resend requests.",
                  conn->last_seqno_read + 1, play_number, 0u, 0u);
            conn->last_seqno_read++; // manage the packet out of sequence minder

            auto silence = conn->pcmEncoder.silence(conn->frames_per_packet);
            config.output->play(silence.bytes().data(), silence.frames(),
                                play_samples_are_untimed, 0, 0);
            frames_played += silence.frames();
          } else {
            EncodedPcm encoded;
            const auto mode = playbackModeFor(config.playback_mode);
            const auto &nativeAudio = inframe->convertedAudio();
            if (!conn->playbackSamples.prepare(nativeAudio, mode))
              die("Incoherent converted PCM payload.");
            const auto shape = nativeAudio.shape();
            if (!conn->pcmEncoder.configure(
                    {FORMAT_FROM_ENCODED_FORMAT(config.current_output_configuration), shape.channels()},
                    shape.effectiveBits()))
              die("Unsupported PCM output format.");
            inbuflength = playback.frames;

            // We have a frame of data. We need to see if we want to add or remove a frame from
            // it to keep in sync. So we calculate the timing error for the first frame in the
            // DAC. If it's ahead of time, we add one audio frame to this frame to delay a
            // subsequent frame If it's late, we remove an audio frame from this frame to bring
            // a subsequent frame forward in time

            // now, go back as far as the total latency less, say, 100 ms, and check the
            // presence of frames from then onwards

            const auto occupancy = conn->packetBuffer.occupancy();
            const bool firstObservedFrame = conn->statistics.observeFrame(occupancy);
            const unsigned outputRate = RATE_FROM_ENCODED_FORMAT(config.current_output_configuration);
            if (conn->statistics.intervalDue(outputRate)) {
              if (config.output->delay && config.output->stats) {
                OutputReading reading{};
                reading.status = config.output->stats(&reading.rawTime, &reading.correctedTime,
                    &reading.queuedFrames, &reading.sentFrames);
                conn->statistics.recordOutputReading(reading);
              }
              if (const auto interval = conn->statistics.takeIntervalIfDue(outputRate);
                  interval && config.statistics_requested) {
                if (interval->hasObservedFrame) {
                  StatisticsFormatter formatter({conn->airplay_stream_type == realtime_stream ?
                      StatisticsStream::realtime : StatisticsStream::buffered,
                      config.output->delay != nullptr, config.output->stats != nullptr,
                      debug_level() != 0});
                  if (!statisticsHeaderPrinted) {
                    inform("%s", formatter.header().c_str());
                    statisticsHeaderPrinted = true;
                  }
                  inform("%s", formatter.row(*interval).c_str());
                } else {
                  inform("No frames received in the last sampling interval.");
                }
              }
            }

            if (firstObservedFrame) {
              char short_description[256];
              snprintf(short_description, sizeof(short_description), "%u/%s/%u",
                       RATE_FROM_ENCODED_FORMAT(config.current_output_configuration),
                       sps_format_description_string(
                           FORMAT_FROM_ENCODED_FORMAT(config.current_output_configuration)),
                       CHANNELS_FROM_ENCODED_FORMAT(config.current_output_configuration));
              // since this is the first frame of audio, inform the user if requested...
              if (conn->airplay_stream_type == realtime_stream) {

                  if (config.statistics_requested) {
                    if (conn->ap2_client_name == NULL)
                      inform("Connection %d: AirPlay 2 Realtime playback. "
                             "Input format: %s. Output format: %s.",
                             conn->connection_number, incomingAudioName(*conn), "");
                    else
                      inform("Connection %d: AirPlay 2 Realtime playback. "
                             "Source: \"%s\". Input format: %s. Output format: %s.",
                             conn->connection_number, conn->ap2_client_name,
                             incomingAudioName(*conn), short_description);
                  }

              } else {

                if (config.statistics_requested) {

                  if (conn->ap2_client_name == NULL)
                    inform("Connection %d: AirPlay 2 Buffered playback. "
                           "Input format: %s. Output format: %s.",
                           conn->connection_number, incomingAudioName(*conn),
                           short_description);
                  else
                    inform("Connection %d: AirPlay 2 Buffered playback. "
                           "Source: \"%s\". Input format: %s. Output format: %s.",
                           conn->connection_number, conn->ap2_client_name,
                           incomingAudioName(*conn), short_description);
                }
              }

            }

            // here, we want to check (a) if we are meant to do synchronisation,
            // (b) if we have a delay procedure, (c) if we can get the delay.

            // If any of these are false, we don't do any synchronisation stuff

            int resp = -1; // use this as a flag -- if negative, we can't rely on a real known delay
            current_delay = -1; // use this as a failure flag

            // if making the measurements takes too long (e.g. due to scheduling) , don't use
            // it.

            uint64_t mst = get_absolute_time_in_ns(); // measurement start time
            uint64_t delay_measurement_time = 0;
            if (config.output->delay) {
              long l_delay;
              resp = config.output->delay(&l_delay);
              delay_measurement_time =
                  get_absolute_time_in_ns(); // put this after delay() returns, as it may take an
                                             // appreciable amount of time to run
              if (resp == 0) {               // no error
                current_delay = l_delay;
                if (l_delay >= 0)
                  current_delay = l_delay;
                else {
                  debug(2, "Underrun of %ld frames reported, but ignored.", l_delay);
                  current_delay =
                      0; // could get a negative value if there was underrun, but ignore it.
                }
                conn->statistics.recordDacQueue(current_delay);
              } else {
                current_delay = 0;
                if ((resp == sps_extra_code_output_stalled) &&
                    (config.unfixable_error_reported == 0)) {
                  config.unfixable_error_reported = 1;
                  if (config.cmd_unfixable) {
                    warn("Connection %d: An unfixable error has been detected -- output device "
                         "is "
                         "stalled. Executing the "
                         "\"run_this_if_an_unfixable_error_is_detected\" command.",
                         conn->connection_number);
                    command_execute(config.cmd_unfixable, "output_device_stalled", 1);
                  } else {
                    warn("Connection %d: An unfixable error has been detected -- output device "
                         "is "
                         "stalled. \"No "
                         "run_this_if_an_unfixable_error_is_detected\" command provided -- "
                         "nothing "
                         "done.",
                         conn->connection_number);
                  }
                } else {
                  if ((resp != -EBUSY) &&
                      (resp != -ENODEV)) // delay and not-there errors can be reported if the
                                         // device is (hopefully temporarily) busy or unavailable
                                         // Note: ENODATA (a better fit) is not availabe in FreeBSD.
                    debug(1, "Delay error %d when checking running latency.", resp);
                }
              }
              // debug(1, "resp is %d, delay is %ld.", resp, l_delay);
            }
            if (resp == 0) {
              uint64_t expectedTime;
              frame_to_local_time(playback.timestamp, &expectedTime, conn);
              SyncObservation observation;
              observation.expectedFrameTime = expectedTime;
              observation.dacMeasurementTime = delay_measurement_time;
              observation.measurementDuration =
                  std::bit_cast<int64_t>(get_absolute_time_in_ns() - mst);
              observation.framesInDac = current_delay;
              observation.retainedFrames = nativeAudio.retainedFrames();
              observation.timestamp = playback.timestamp;
              observation.timestampGap = playback.timestampGap;
              observation.firstFrame = conn->first_packet_timestamp == playback.timestamp;
              observation.measured = true;
              observation.inputRate = conn->input_rate;
              observation.outputRate = RATE_FROM_ENCODED_FORMAT(config.current_output_configuration);
              observation.blockFrames = inbuflength;
              observation.playNumber = play_number;
              const SyncPolicy policy{config.no_sync == 0,
                  static_cast<int64_t>(config.tolerance * 1000000000),
                  config.resync_threshold};
              const auto decision = conn->playbackSync.observe(observation, policy);
              sync_error = decision.errorFrames;
              amount_to_stuff = decision.correctionFrames;
              conn->statistics.recordSync(decision);
              if (decision.silenceFrames) {
                auto silence = conn->pcmEncoder.silence(decision.silenceFrames);
                config.output->play(silence.bytes().data(), silence.frames(),
                                    play_samples_are_untimed, 0, 0);
                frames_played += silence.frames();
              }
              if (decision.resyncNext)
                request_resync = 1;
              if (!decision.dropPacket) {
                encoded = conn->playbackSamples.encode(conn->pcmEncoder,
                    {config.packet_stuffing == ST_basic ? CorrectionStyle::basic :
                                                         CorrectionStyle::vernier, amount_to_stuff});


                play_samples = encoded.frames();
                if (encoded.bytes().empty())
                  debug(1, "No encoded PCM to play -- skipping it.");
                else {
                  if (play_samples == 0)
                    debug(2, "nothing to play.");
                  else {
                    if (pcmVolume.softwareMuted) {
                      encoded = conn->pcmEncoder.silence(play_samples);
                    }
                    uint64_t should_be_time;
                    frame_to_local_time(playback.timestamp, &should_be_time, conn);
                    // debug(1, "play frame %u.", playback.timestamp);

                    const auto discard = conn->playbackSync.skipFrom(play_samples);
                    if (discard.submitBlock) {
                      const size_t bytesToSkip = discard.frames *
                          CHANNELS_FROM_ENCODED_FORMAT(config.current_output_configuration) *
                          sps_format_sample_size(FORMAT_FROM_ENCODED_FORMAT(config.current_output_configuration));
                      config.output->play(encoded.bytes().data() + bytesToSkip,
                                          play_samples - discard.frames, play_samples_are_timed,
                                          playback.timestamp, should_be_time);
                      frames_played += play_samples - discard.frames;
                    }

                  }
                }
              }
            } else {

              // if this is the first frame, see if it's close to when it's supposed to be
              // released, which will be its time plus latency and any offset_time

              encoded = conn->playbackSamples.encode(conn->pcmEncoder,
                  {config.packet_stuffing == ST_basic ? CorrectionStyle::basic :
                                                       CorrectionStyle::vernier, 0});
              play_samples = encoded.frames();
              if (encoded.bytes().empty())
                debug(1, "No encoded PCM to play -- skipping it.");
              else {
                if (pcmVolume.softwareMuted) {
                  encoded = conn->pcmEncoder.silence(play_samples);
                }
                uint64_t should_be_time;
                frame_to_local_time(playback.timestamp, &should_be_time, conn);
                debug(3, "play frame %u.", playback.timestamp);
                config.output->play(encoded.bytes().data(), play_samples, play_samples_are_timed,
                                    playback.timestamp, should_be_time);
                frames_played += play_samples;
              }
            }


          }
          conn->statistics.recordSubmitted(frames_played, sync_error, amount_to_stuff);
        }
      } else {
        debug(1, "audio block sequence number %u, ready status: %u with no data!",
              playback.sequence, playback.ready);
      }
      inframe.reset();
    }
  }

  debug(1, "This should never be called.");
  pthread_cleanup_pop(1); // pop the cleanup handler
                          //  debug(1, "This should never be called either.");
                          //  pthread_cleanup_pop(1); // pop the initial cleanup handler
  pthread_exit(NULL);
}

static void applyVolumePlan(double level, rtsp_conn_info *conn) {
  VolumeSettings settings;
  switch (config.volume_control_profile) {
  case VCP_standard: settings.profile = VolumeProfile::standard; break;
  case VCP_flat: settings.profile = VolumeProfile::flat; break;
  case VCP_dasl_tapered: settings.profile = VolumeProfile::dasl; break;
  }
  if (config.volume_max_db_set) settings.maximumDb = config.volume_max_db;
  settings.rangeDb = config.volume_range_db;
  settings.hardwarePriority = config.volume_range_hw_priority != 0;
  settings.ignoreControl = config.ignore_volume_control != 0;
  OutputVolumeCapabilities capabilities;
  capabilities.canSetHardwareVolume = config.output->volume != nullptr;
  if (config.output->parameters) {
    if (const auto parameters = config.output->parameters(); parameters && parameters->volume_range)
      capabilities.range = VolumeRange{parameters->volume_range->minimum_volume_dB,
                                      parameters->volume_range->maximum_volume_dB};
  }
  const auto plan = VolumePolicy::plan(level, settings, capabilities);
  if (plan.maximumIgnored)
    warn("The maximum output level is outside the range of the hardware mixer -- ignored");
  if (plan.rangeIgnored)
    warn("The range requested is too large to accommodate -- ignored.");
  bool hardwareMuted = false;
  if (plan.requestMute && config.output->mute) hardwareMuted = config.output->mute(1) == 0;
  if (plan.hardwareAttenuation) config.output->volume(*plan.hardwareAttenuation);
  conn->volumeControl.apply(plan, hardwareMuted);
  if (level != -144 && config.logOutputLevel)
    inform("Output Level set to: %.2f dB.", plan.scaledAttenuation / 100);
  if (plan.unmute && config.output->mute) config.output->mute(0);
}

void applySessionVolume(double level, SessionState &session) {
  command_set_volume(level);
  applySessionVolumeEffects(level, session);
}

void applySessionVolumeEffects(double level, SessionState &session) {
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  applyVolumePlan(level, &session);
  pthread_setcancelstate(previousState, nullptr);
}

void player_volume_without_notification(double level, rtsp_conn_info *conn) {
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  applyVolumePlan(level, conn);
  sharedVolumeLevel.remember(level);
  pthread_setcancelstate(previousState, nullptr);
}

void player_volume(double level, rtsp_conn_info *conn) {
  command_set_volume(level);
  player_volume_without_notification(level, conn);
}

void do_flush(uint32_t timestamp, rtsp_conn_info *conn) {

  debug(3, "do_flush: flush to %u.", timestamp);
  conn->packetBuffer.requestFlush(timestamp);
  reset_input_flow_metrics(conn);
}

void player_flush(uint32_t timestamp, rtsp_conn_info *conn) {
  debug(3, "player_flush");
  do_flush(timestamp, conn);
}

int player_play(rtsp_conn_info *conn) {
  debug(2, "Connection %d: player_play.", conn->connection_number);
  command_start(); // before startup, and before the prepare() method runs
  // give the output device as much advance warning as possible to get ready
  // and make sure it's done before launching the player thread
  if (config.output->prepare)
    config.output->prepare(); // give the backend its first chance to prepare itself, knowing it has
                              // access to the output device (i.e. knowing that it should not be in
                              // use by another program at this time).

  pthread_mutex_lock_and_cleanup_push(&conn->player_create_delete_mutex);
  if (conn->player_thread == NULL) {
    pthread_t *pt = static_cast<pthread_t *>(malloc(sizeof(pthread_t)));
    if (pt == NULL)
      die("Couldn't allocate space for pthread_t");

    int rc = named_pthread_create_with_priority(pt, 3, player_thread_func, (void *)conn,
                                                "player_%d", conn->connection_number);
    if (rc)
      debug(1, "Connection %d: error creating player_thread: %s", conn->connection_number,
            strerror(errno));
    conn->player_thread = pt; // set _after_ creation of thread
  } else {
    debug(1, "Connection %d: player thread already exists.", conn->connection_number);
  }
  pthread_cleanup_pop(1); // release the player_create_delete_mutex
  conn->is_playing = 1;
  return 0;
}

int player_stop(rtsp_conn_info *conn) {
  // note -- this may be called from another connection thread.
  debug(2, "Connection %d: player_stop.", conn->connection_number);
  int response = 0; // okay
  pthread_mutex_lock_and_cleanup_push(&conn->player_create_delete_mutex);
  pthread_t *pt = conn->player_thread;
  if (pt) {
    debug(3, "player_thread cancel...");
    conn->player_thread = NULL; // cleared _before_ cancelling of thread
    pthread_cancel(*pt);
    debug(3, "player_thread join...");
    if (pthread_join(*pt, NULL) == -1) {
      char errorstring[1024];
      strerror_r(errno, (char *)errorstring, sizeof(errorstring));
      debug(1, "Connection %d: error %d joining player thread: \"%s\".", conn->connection_number,
            errno, (char *)errorstring);
    } else {
      debug(2, "Connection %d: player_stop successful.", conn->connection_number);
    }
    free(pt);
    // reset_anchor_info(conn); // say the clock is no longer valid
    response = 0; // deleted
  } else {
    debug(2, "Connection %d: no player thread.", conn->connection_number);
    response = -1; // already deleted or never created...
  }
  pthread_cleanup_pop(1); // release the player_create_delete_mutex
  if (response == 0) {    // if the thread was just stopped and deleted...
    conn->is_playing = 0;
/*
// this is done in the player cleanup handler
#ifdef CONFIG_AIRPLAY_2
    ptp_send_control_message_string("E"); // signify play is "E"nding
#endif
*/
    command_stop();
  }
  return response;
}
