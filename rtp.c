/*
 * Apple RTP protocol handler. This file is part of Shairport.
 * Copyright (c) James Laird 2013
 * Copyright (c) Mike Brady 2014--2025
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

#include "rtp.h"
#include "common.h"
#include "player.h"
#include "rtsp.h"
#include "utilities/network_utilities.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <memory.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// #include "plist_xml_strings.h"
#include "ptp-utilities.h"
#include "utilities/structured_buffer.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <sodium.h>



/*
      char obf[4096];
      char *obfp = obf;
      size_t obfc;
      for (obfc=0; obfc < strlen(buffer); obfc++) {
        snprintf(obfp, 3, "%02X", buffer[obfc]);
        obfp+=2;
      };
      *obfp=0;
      debug(1,"Writing: \"%s\"",obf);

*/

void check64conversion(const char *prompt, const uint8_t *source, uint64_t value) {
  char converted_value[128];
  sprintf(converted_value, "%" PRIx64 "", value);

  char obf[32];
  char *obfp = obf;
  int obfc;
  int suppress_zeroes = 1;
  for (obfc = 0; obfc < 8; obfc++) {
    if ((suppress_zeroes == 0) || (source[obfc] != 0)) {
      if (suppress_zeroes != 0) {
        if (source[obfc] < 0x10) {
          snprintf(obfp, 3, "%1x", source[obfc]);
          obfp += 1;
        } else {
          snprintf(obfp, 3, "%02x", source[obfc]);
          obfp += 2;
        }
      } else {
        snprintf(obfp, 3, "%02x", source[obfc]);
        obfp += 2;
      }
      suppress_zeroes = 0;
    }
  };
  *obfp = 0;
  if (strcmp(converted_value, obf) != 0) {
    debug(1, "%s check64conversion error converting \"%s\" to %" PRIx64 ".", prompt, obf, value);
  }
}

void check32conversion(const char *prompt, const uint8_t *source, uint32_t value) {
  char converted_value[128];
  sprintf(converted_value, "%" PRIx32 "", value);

  char obf[32];
  char *obfp = obf;
  int obfc;
  int suppress_zeroes = 1;
  for (obfc = 0; obfc < 4; obfc++) {
    if ((suppress_zeroes == 0) || (source[obfc] != 0)) {
      if (suppress_zeroes != 0) {
        if (source[obfc] < 0x10) {
          snprintf(obfp, 3, "%1x", source[obfc]);
          obfp += 1;
        } else {
          snprintf(obfp, 3, "%02x", source[obfc]);
          obfp += 2;
        }
      } else {
        snprintf(obfp, 3, "%02x", source[obfc]);
        obfp += 2;
      }
      suppress_zeroes = 0;
    }
  };
  *obfp = 0;
  if (strcmp(converted_value, obf) != 0) {
    debug(1, "%s check32conversion error converting \"%s\" to %" PRIx32 ".", prompt, obf, value);
  }
}

void rtp_initialise(rtsp_conn_info *conn) {
  conn->rtp_time_of_last_resend_request_error_ns = 0;
  conn->rtp_running = 0;
  // initialise the timer mutex
  int rc = pthread_mutex_init(&conn->reference_time_mutex, NULL);
  if (rc)
    debug(1, "Error initialising reference_time_mutex.");
}

void rtp_terminate(rtsp_conn_info *conn) {
  conn->anchor_rtptime = 0;
  // destroy the timer mutex
  int rc = pthread_mutex_destroy(&conn->reference_time_mutex);
  if (rc)
    debug(1, "Error destroying reference_time_mutex variable.");
}

void rtp_request_resend(seq_t first, uint32_t count, rtsp_conn_info *conn) {
  // debug(1, "rtp_request_resend of %u packets from sequence number %u.", count, first);
  if (conn->rtp_running) {
    // if (!request_sent) {
    // debug(2, "requesting resend of %d packets starting at %u.", count, first);
    //  request_sent = 1;
    //}

    char req[8]; // *not* a standard RTCP NACK
    req[0] = 0x80;

      if (conn->ap2_remote_control_socket_addr_length == 0) {
        debug(2, "No remote socket -- skipping the resend");
        return; // hack
      }
      req[1] = 0xD5; // Airplay 2 'resend'

    *(unsigned short *)(req + 2) = htons(1);     // our sequence number
    *(unsigned short *)(req + 4) = htons(first); // missed seqnum
    *(unsigned short *)(req + 6) = htons(count); // count

    uint64_t time_of_sending_ns = get_absolute_time_in_ns();
    uint64_t resend_error_backoff_time = 300000000; // 0.3 seconds
    if ((conn->rtp_time_of_last_resend_request_error_ns == 0) ||
        ((time_of_sending_ns - conn->rtp_time_of_last_resend_request_error_ns) >
         resend_error_backoff_time)) {
      if ((config.diagnostic_drop_packet_fraction == 0.0) ||
          (drand48() > config.diagnostic_drop_packet_fraction)) {
        // put a time limit on the sendto

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        int response;

          if (setsockopt(conn->ap2_control_socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,
                         sizeof(timeout)) < 0)
            debug(1, "Can't set timeout on resend request socket.");
          response = sendto(conn->ap2_control_socket, req, sizeof(req), 0,
                            (struct sockaddr *)&conn->ap2_remote_control_socket_addr,
                            conn->ap2_remote_control_socket_addr_length);

        if (response == -1) {
          char em[1024];
          strerror_r(errno, em, sizeof(em));
          debug(2, "Error %d using sendto to request a resend: \"%s\".", errno, em);
          conn->rtp_time_of_last_resend_request_error_ns = time_of_sending_ns;
        } else {
          conn->rtp_time_of_last_resend_request_error_ns = 0;
        }

      } else {
        debug(3, "Dropping resend request packet to simulate a bad network. Backing off for 0.3 "
                 "second.");
        conn->rtp_time_of_last_resend_request_error_ns = time_of_sending_ns;
      }
    } else {
      debug(1,
            "Suppressing a resend request due to a resend sendto error in the last 0.3 seconds.");
    }
  } else {
    // if (!request_sent) {
    debug(2, "rtp_request_resend called without active stream!");
    //  request_sent = 1;
    //}
  }
}


void set_ptp_anchor_info(rtsp_conn_info *conn, uint64_t clock_id, uint32_t rtptime,
                         uint64_t networktime) {
  if ((conn->anchor_clock != 0) && (conn->anchor_clock == clock_id) &&
      (conn->anchor_remote_info_is_valid != 0)) {
    // check change in timing
    int64_t time_difference = networktime - conn->anchor_time;
    int32_t frame_difference = rtptime - conn->anchor_rtptime;
    double time_difference_in_frames = (1.0 * time_difference * conn->input_rate) / 1000000000;
    double frame_change = frame_difference - time_difference_in_frames;
    debug(3,
          "Connection %d: set_ptp_anchor_info: clock: %" PRIx64 ", rtptime: %" PRIu32
          ", networktime: %" PRIx64 ", frame adjustment: %7.3f.",
          conn->connection_number, clock_id, rtptime, networktime, frame_change);
  } else {
    debug(2,
          "Connection %d: set_ptp_anchor_info: clock: %" PRIx64 ", rtptime: %" PRIu32
          ", networktime: %" PRIx64 ".",
          conn->connection_number, clock_id, rtptime, networktime);
  }
  if (conn->anchor_clock != clock_id) {
    debug(2, "Connection %d: Set Anchor Clock: %" PRIx64 ".", conn->connection_number, clock_id);
  }
  // debug(1,"set anchor info clock: %" PRIx64", rtptime: %u, networktime: %" PRIx64 ".", clock_id,
  // rtptime, networktime);

  // if the clock is the same but any details change, and if the last_anchor_info has not been
  // valid for some minimum time (and thus may not be reliable), we need to invalidate
  // last_anchor_info

  if ((conn->airplay_stream_type == buffered_stream) && (conn->ap2_play_enabled != 0) &&
      ((clock_id != conn->anchor_clock) || (conn->anchor_rtptime != rtptime) ||
       (conn->anchor_time != networktime))) {
    uint64_t master_clock_id = 0;
    ptp_get_clock_info(&master_clock_id, NULL, NULL, NULL);
    debug(1,
          "Connection %d: Note: anchor parameters have changed. Old clock: %" PRIx64
          ", rtptime: %u, networktime: %" PRIu64 ". New clock: %" PRIx64
          ", rtptime: %u, networktime: %" PRIu64 ". Current master clock: %" PRIx64 ".",
          conn->connection_number, conn->anchor_clock, conn->anchor_rtptime, conn->anchor_time,
          clock_id, rtptime, networktime, master_clock_id);
  }

  if ((clock_id == conn->anchor_clock) &&
      ((conn->anchor_rtptime != rtptime) || (conn->anchor_time != networktime))) {
    uint64_t time_now = get_absolute_time_in_ns();
    int64_t last_anchor_validity_duration = time_now - conn->last_anchor_validity_start_time;
    if (last_anchor_validity_duration < 5000000000) {
      if (conn->airplay_stream_type == buffered_stream)
        debug(2,
              "Connection %d: Note: anchor parameters have changed before clock %" PRIx64
              " has stabilised.",
              conn->connection_number, clock_id);
      conn->last_anchor_info_is_valid = 0;
    }
  }

  conn->anchor_remote_info_is_valid = 1;

  // these can be modified if the master clock changes over time

  conn->anchor_rtptime = rtptime;
  conn->anchor_time = networktime;
  conn->anchor_clock = clock_id;
  debug(2, "set_ptp_anchor_info done.");
}

int long_time_notifcation_done = 0;

uint64_t previous_offset = 0;
uint64_t previous_clock_id = 0;

void reset_ptp_anchor_info(rtsp_conn_info *conn) {
  debug(2, "Connection %d: Clear anchor information.", conn->connection_number);
  conn->last_anchor_info_is_valid = 0;
  conn->anchor_remote_info_is_valid = 0;
  long_time_notifcation_done = 0;
  previous_offset = 0;
  previous_clock_id = 0;
}

int get_ptp_anchor_local_time_info(rtsp_conn_info *conn, uint32_t *anchorRTP,
                                   uint64_t *anchorLocalTime) {
  int response = clock_no_anchor_info; // no anchor information
  if (conn->anchor_remote_info_is_valid != 0) {
    response = clock_not_valid;
    uint64_t actual_clock_id;
    uint64_t actual_time_of_sample, actual_offset, start_of_mastership;
    response = ptp_get_clock_info(&actual_clock_id, &actual_time_of_sample, &actual_offset,
                                  &start_of_mastership);
    if (response == clock_ok) {
      uint64_t time_now = get_absolute_time_in_ns();
      int64_t time_since_start_of_mastership = time_now - start_of_mastership;
      if (time_since_start_of_mastership >= 400000000L) {
        int64_t time_since_sample = time_now - actual_time_of_sample;
        if (time_since_sample > 300000000000L) {
          if (long_time_notifcation_done == 0) {
            debug(1, "The last PTP timing sample is pretty old: %f seconds.",
                  0.000000001 * time_since_sample);
            long_time_notifcation_done = 1;
          }
        } else if ((time_since_sample < 2000000000) && (long_time_notifcation_done != 0)) {
          debug(1, "The last PTP timing sample is no longer too old: %f seconds.",
                0.000000001 * time_since_sample);
          long_time_notifcation_done = 0;
        }

        int64_t jitter = actual_offset - previous_offset;

        if ((previous_offset != 0) && (previous_clock_id == actual_clock_id) &&
            ((jitter > 3000000) || (jitter < -3000000)))
          debug(1,
                "Clock jitter: %.3f mS. Time since sample: %.3f mS. Time since start of "
                "mastership: %.3f "
                "seconds.",
                jitter * 0.000001, time_since_sample * 0.000001,
                time_since_start_of_mastership * 0.000000001);

        previous_offset = actual_offset;
        previous_clock_id = actual_clock_id;

        if (actual_clock_id == conn->anchor_clock) {
          conn->last_anchor_rtptime = conn->anchor_rtptime;
          conn->last_anchor_local_time = conn->anchor_time - actual_offset;
          conn->last_anchor_time_of_update = time_now;
          if (conn->last_anchor_info_is_valid == 0)
            conn->last_anchor_validity_start_time = start_of_mastership;
          conn->last_anchor_info_is_valid = 1;
        } else {
          debug(3, "Current master clock %" PRIx64 " and anchor_clock %" PRIx64 " are different",
                actual_clock_id, conn->anchor_clock);
          // the anchor clock and the actual clock are different

          if (conn->last_anchor_info_is_valid != 0) {

            int64_t time_since_last_update =
                get_absolute_time_in_ns() - conn->last_anchor_time_of_update;
            if (time_since_last_update > 5000000000) {
              int64_t duration_of_mastership = time_now - start_of_mastership;
              debug(2,
                    "Connection %d: Master clock has changed to %" PRIx64
                    ". History: %.3f milliseconds.",
                    conn->connection_number, actual_clock_id, 0.000001 * duration_of_mastership);

              // Now, the thing is that while the anchor clock and master clock for a
              // buffered session start off the same,
              // the master clock can change without the anchor clock changing.
              // SPS gives the new master clock time to settle down and then
              // calculates the appropriate offset to it by
              // calculating back from the local anchor information and the new clock's
              // advertised offset.

              conn->anchor_time = conn->last_anchor_local_time + actual_offset;
              conn->anchor_clock = actual_clock_id;
            }

          } else {
            response = clock_not_valid; // no current clock information and no previous clock info
          }
        }

      } else {
        // debug(1, "mastership time: %f s.", time_since_start_of_mastership * 0.000000001);
        response = clock_not_valid; // hasn't been master for long enough...
      }
    }

    // here, check and update the clock status
    if ((clock_status_t)response != conn->clock_status) {
      switch (response) {
      case clock_ok:
        debug(2, "Connection %d: NQPTP master clock %" PRIx64 ".", conn->connection_number,
              actual_clock_id);
        break;
      case clock_not_ready:
        debug(2, "Connection %d: NQPTP master clock %" PRIx64 " is available but not ready.",
              conn->connection_number, actual_clock_id);
        break;
      case clock_service_unavailable:
        debug(1, "Connection %d: NQPTP clock is not available.", conn->connection_number);
        warn("Can't access the NQPTP clock. Is NQPTP running?");
        break;
      case clock_access_error:
        debug(2, "Connection %d: Error accessing the NQPTP clock interface.",
              conn->connection_number);
        break;
      case clock_data_unavailable:
        debug(1, "Connection %d: Can not access NQPTP clock information.", conn->connection_number);
        break;
      case clock_no_master:
        debug(2, "Connection %d: No NQPTP master clock.", conn->connection_number);
        break;
      case clock_no_anchor_info:
        debug(2, "Connection %d: Awaiting clock anchor information.", conn->connection_number);
        break;
      case clock_version_mismatch:
        debug(2, "Connection %d: NQPTP clock interface mismatch.", conn->connection_number);
        warn(
            "This version of Shairport Sync is not compatible with the installed version of NQPTP. "
            "Please update.");
        break;
      case clock_not_synchronised:
        debug(1, "Connection %d: NQPTP clock is not synchronised.", conn->connection_number);
        break;
      case clock_not_valid:
        debug(2, "Connection %d: NQPTP clock information is not valid.", conn->connection_number);
        break;
      default:
        debug(1, "Connection %d: NQPTP clock reports an unrecognised status: %u.",
              conn->connection_number, response);
        break;
      }
      conn->clock_status = response;
    }

    if (conn->last_anchor_info_is_valid != 0) {
      if (anchorRTP != NULL) {
        // Use the current rate in case the stream format has changed.
        int32_t added_latency = (int32_t)(config.audio_backend_latency_offset * conn->input_rate);
        *anchorRTP = conn->last_anchor_rtptime - added_latency;
      }
      if (anchorLocalTime != NULL)
        *anchorLocalTime = conn->last_anchor_local_time;
    }
  }
  return response;
}

int have_ptp_timing_information(rtsp_conn_info *conn) {
  if (get_ptp_anchor_local_time_info(conn, NULL, NULL) == clock_ok)
    return 1;
  else
    return 0;
}

int frame_to_ptp_local_time(uint32_t timestamp, uint64_t *time, rtsp_conn_info *conn) {
  int result = -1;
  uint32_t anchor_rtptime = 0;
  uint64_t anchor_local_time = 0;
  if ((conn->input_rate != 0) && (get_ptp_anchor_local_time_info(conn, &anchor_rtptime, &anchor_local_time) == clock_ok)) {
    int32_t frame_difference = timestamp - anchor_rtptime;
    int64_t time_difference = frame_difference;
    time_difference = time_difference * 1000000000;
    time_difference = time_difference / conn->input_rate;
    uint64_t ltime = anchor_local_time + time_difference;
    *time = ltime;
    result = 0;
  } else {
    debug(4, "frame_to_ptp_local_time can't get anchor local time information");
  }
  return result;
}

int local_ptp_time_to_frame(uint64_t time, uint32_t *frame, rtsp_conn_info *conn) {
  int result = -1;
  uint32_t anchor_rtptime = 0;
  uint64_t anchor_local_time = 0;
  if ((conn->input_rate != 0) && (get_ptp_anchor_local_time_info(conn, &anchor_rtptime, &anchor_local_time) == clock_ok)) {
    int64_t time_difference = time - anchor_local_time;
    int64_t frame_difference = time_difference;
    frame_difference = frame_difference * conn->input_rate; // but this is by 10^9
    frame_difference = frame_difference / 1000000000;
    int32_t fd32 = frame_difference;
    uint32_t lframe = anchor_rtptime + fd32;
    *frame = lframe;
    result = 0;
  } else {
    debug(2, "local_ptp_time_to_frame can't get anchor local time information");
  }
  return result;
}

void rtp_ap2_control_handler_cleanup_handler(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  debug(2, "Connection %d: AP2 Control Receiver Cleanup.", conn->connection_number);
  safe_socket_close(&conn->ap2_control_socket);
  debug(2, "Connection %d: UDP control port %u closed.", conn->connection_number,
        conn->local_ap2_control_port);
  conn->ap2_remote_control_socket_addr_length =
      0; // indicates to the control receiver thread that the socket address need to be
         // recreated (needed for resend requests in the realtime mode)
}

int32_t decipher_player_put_packet(uint8_t *ciphered_audio_alt, ssize_t nread,
                                   rtsp_conn_info *conn) {

  // this deciphers the packet -- it doesn't decode it from ALAC
  uint16_t sequence_number = 0;

  // if the packet is too small, don't go ahead.
  // it must contain an uint16_t sequence number and eight bytes of AAD followed by the
  // ciphertext and then followed by an eight-byte nonce. Thus it must be greater than 18
  if (nread > 18) {

    memcpy(&sequence_number, ciphered_audio_alt, sizeof(uint16_t));
    sequence_number = ntohs(sequence_number);

    uint32_t timestamp;
    memcpy(&timestamp, ciphered_audio_alt + sizeof(uint16_t), sizeof(uint32_t));
    timestamp = ntohl(timestamp);

    if (conn->session_key != NULL) {
      unsigned char nonce[12];
      memset(nonce, 0, sizeof(nonce));
      memcpy(nonce + 4, ciphered_audio_alt + nread - 8,
             8); // front-pad the 8-byte nonce received to get the 12-byte nonce expected

      // https://libsodium.gitbook.io/doc/secret-key_cryptography/aead/chacha20-poly1305/ietf_chacha20-poly1305_construction
      // Note: the eight-byte nonce must be front-padded out to 12 bytes.

      unsigned char m[4096];
      unsigned long long new_payload_length = 0;
      int response = crypto_aead_chacha20poly1305_ietf_decrypt(
          m,                   // m
          &new_payload_length, // mlen_p
          NULL,                // nsec,
          ciphered_audio_alt +
              10,           // the ciphertext starts 10 bytes in and is followed by the MAC tag,
          nread - (8 + 10), // clen -- the last 8 bytes are the nonce
          ciphered_audio_alt + 2, // authenticated additional data
          8,                      // authenticated additional data length
          nonce,
          conn->session_key); // *k
      if (response != 0) {
        debug(1, "Error decrypting an audio packet.");
      }
      // now pass it in to the regular processing chain

      unsigned long long max_int = INT_MAX; // put in the right format
      if (new_payload_length > max_int)
        debug(1, "Madly long payload length!");
      int plen = new_payload_length; //
      // debug(1,"                                                        Write packet to buffer %d,
      // timestamp %u.", sequence_number, timestamp);
      player_put_packet(ALAC_44100_S16_2, sequence_number, timestamp, m, plen, 0, 0,
                        conn); // 0 = no mute, 0 = non discontinuous
    } else {
      debug(2, "No session key, so the audio packet can not be deciphered -- skipped.");
    }
    return sequence_number;
  } else {
    debug(1, "packet was too small -- ignored");
    return -1;
  }
}

void *rtp_ap2_control_receiver(void *arg) {
  const int32_t ap2_realttime_stream_latency_fudge_factor = 11025; // seems to bring everything into sync
  //  #include <syscall.h>
  //  debug(1, "rtp_ap2_control_receiver PID %d", syscall(SYS_gettid));
  pthread_cleanup_push(rtp_ap2_control_handler_cleanup_handler, arg);
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  uint8_t packet[4096];
  ssize_t nread;
  int keep_going = 1;
  uint64_t start_time = get_absolute_time_in_ns();
  uint64_t packet_number = 0;
  while (keep_going) {
    SOCKADDR from_sock_addr;
    socklen_t from_sock_addr_length = sizeof(SOCKADDR);
    memset(&from_sock_addr, 0, sizeof(SOCKADDR));

    nread = recvfrom(conn->ap2_control_socket, packet, sizeof(packet), 0,
                     (struct sockaddr *)&from_sock_addr, &from_sock_addr_length);
    uint64_t time_now = get_absolute_time_in_ns();
    int64_t time_since_start = time_now - start_time;

    if (conn->udp_clock_is_initialised == 0) {
      packet_number = 0;
      conn->udp_clock_is_initialised = 1;
      debug(2, "AP2 Realtime Clock receiver initialised.");
    }

    // debug(1,"Connection %d: AP2 Control Packet received.", conn->connection_number);

    if (nread >= 28) { // must have at least 28 bytes for the timing information
      if ((time_since_start < 2000000) && ((packet[0] & 0x10) == 0)) {
        debug(1,
              "Dropping what looks like a (non-sentinel) packet left over from a previous session "
              "at %f ms.",
              0.000001 * time_since_start);
      } else {
        packet_number++;
        // debug(1,"AP2 Packet %" PRIu64 ".", packet_number);

        if (packet_number == 1) {
          if ((packet[0] & 0x10) != 0) {
            debug(2, "First packet is a sentinel packet.");
          } else {
            debug(2, "First packet is a not a sentinel packet!");
          }
        }
        // debug(1,"rtp_ap2_control_receiver coded: %u, %u", packet[0], packet[1]);
        // you might want to set this higher to specify how many initial timings to ignore
        if (packet_number >= 1) {
          if ((config.diagnostic_drop_packet_fraction == 0.0) ||
              (drand48() > config.diagnostic_drop_packet_fraction)) {
            // store the from_sock_addr if we haven't already done so
            // v remember to zero this when you're finished!
            if (conn->ap2_remote_control_socket_addr_length == 0) {
              memcpy(&conn->ap2_remote_control_socket_addr, &from_sock_addr, from_sock_addr_length);
              conn->ap2_remote_control_socket_addr_length = from_sock_addr_length;
            }
            switch (packet[1]) {
            case 215: // code 215, effectively an anchoring announcement
            {
              uint64_t remote_packet_time_ns = nctoh64(packet + 8);
              check64conversion("remote_packet_time_ns", packet + 8, remote_packet_time_ns);
              uint64_t clock_id = nctoh64(packet + 20);
              check64conversion("clock_id", packet + 20, clock_id);

              // debug(1, "we have clock_id: %" PRIx64 ".", clock_id);
              // debug(1,"remote_packet_time_ns: %" PRIx64 ", local_realtime_now_ns: %" PRIx64
              // ".", remote_packet_time_ns, local_realtime_now);
              uint32_t frame_1 =
                  nctohl(packet + 4); // this seems to be the frame with latency of 77175 included
              check32conversion("frame_1", packet + 4, frame_1);
              uint32_t frame_2 =
                  nctohl(packet + 16); // this seems to be the frame the time refers to
              check32conversion("frame_2", packet + 16, frame_2);
              // this updates the anchor information contained in the packet
              int32_t stream_specified_latency = frame_2 - frame_1; // this is the latency expected
              if (stream_specified_latency != 77175)
                debug(1, "Stream-specified latency is %d frames. Normally it is 77175.", stream_specified_latency);
              int32_t net_source_latency = stream_specified_latency + ap2_realttime_stream_latency_fudge_factor;

              // Now to accommodate a backend buffer of the desired length.
              // Note that it's in input-rate frames, not output-rate frames!

              net_source_latency = net_source_latency - (int32_t)(config.audio_backend_buffer_desired_length *
                                                    conn->input_rate);

              // Now we want to check the user-specified latency offset.

              // We want to warn the user if they have asked for a negative latency that is too great --
              // one that would require packets to arrive before they actually do,
              // (which is about two seconds before they are to be played).

              int32_t net_latency = net_source_latency + (int32_t)(config.audio_backend_latency_offset * conn->input_rate);

              if (net_latency <= 0) {
                if (conn->latency_warning_issued == 0) {
                  warn("The stream latency (%g seconds) is too short to accommodate an audio backend latency offset of "
                       "%g seconds and a backend buffer of %g seconds. The audio_backend_latency_offset has been set to zero.",
                       ((stream_specified_latency + ap2_realttime_stream_latency_fudge_factor) * 1.0) / conn->input_rate,
                       config.audio_backend_latency_offset,
                       config.audio_backend_buffer_desired_length);
                  config.audio_backend_latency_offset = 0.0;
                  net_latency = net_source_latency;
                  conn->latency_warning_issued = 1;
                }
              }
              conn->latency = net_latency; // this is the time window within which packets can be accepted without being too late
              set_ptp_anchor_info(conn, clock_id, frame_1 - ap2_realttime_stream_latency_fudge_factor, remote_packet_time_ns);
              if (conn->anchor_clock != clock_id) {
                debug(2, "Connection %d: Change Anchor Clock: %" PRIx64 ".",
                      conn->connection_number, clock_id);
              }

            } break;
            case 0xd6:
              // six bytes in is the sequence number at the start of the encrypted audio packet
              // returns the sequence number but we're not really interested
              decipher_player_put_packet(packet + 6, nread - 6, conn);
              break;
            default: {
              char *packet_in_hex_cstring =
                  debug_malloc_hex_cstring(packet, nread); // remember to free this afterwards
              debug(1,
                    "AP2 Control Receiver Packet of first byte 0x%02X, type 0x%02X length %zd "
                    "received: "
                    "\"%s\".",
                    packet[0], packet[1], nread, packet_in_hex_cstring);
              free(packet_in_hex_cstring);
            } break;
            }
          } else {
            debug(1, "AP2 Control Receiver -- dropping a packet.");
          }
        }
      }
    } else {
      if (nread == -1) {
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
          if (conn->airplay_stream_type == realtime_stream) {
            debug(1,
                  "Connection %d: no control packets for the last 7 seconds -- resetting anchor "
                  "info",
                  conn->connection_number);
            reset_ptp_anchor_info(conn);
            packet_number = 0; // start over in allowing the packet to set anchor information
          }
        } else {
          debug(2, "Connection %d: AP2 Control Receiver -- error %d receiving a packet.",
                conn->connection_number, errno);
        }
      } else {
        debug(2, "Connection %d: AP2 Control Receiver -- malformed packet, %zd bytes long.",
              conn->connection_number, nread);
      }
    }
  }
  debug(1, "AP2 Control RTP thread \"normal\" exit -- this can't happen. Hah!");
  pthread_cleanup_pop(1);
  debug(1, "AP2 Control RTP thread exit.");
  pthread_exit(NULL);
}

void rtp_realtime_audio_cleanup_handler(__attribute__((unused)) void *arg) {
  debug(2, "Realtime Audio Receiver Cleanup Start.");
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  debug(2, "Connection %d: closing realtime audio port %u", conn->connection_number,
        conn->local_realtime_audio_port);
  safe_socket_close(&conn->realtime_audio_socket);
  debug(2, "Realtime Audio Receiver Cleanup Done.");
}

void *rtp_realtime_audio_receiver(void *arg) {
  //  #include <syscall.h>
  //  debug(1, "rtp_realtime_audio_receiver PID %d", syscall(SYS_gettid));
  pthread_cleanup_push(rtp_realtime_audio_cleanup_handler, arg);
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  uint8_t packet[4096];
  int32_t last_seqno = -1;
  ssize_t nread;
  while (1) {
    nread = recv(conn->realtime_audio_socket, packet, sizeof(packet), 0);

    if (nread > 36) { // 36 is the 12-byte header and and 24-byte footer
      if ((config.diagnostic_drop_packet_fraction == 0.0) ||
          (drand48() > config.diagnostic_drop_packet_fraction)) {

        /*
                char *packet_in_hex_cstring =
                    debug_malloc_hex_cstring(packet, nread); // remember to free this afterwards
                debug(1, "Audio Receiver Packet of type 0x%02X length %d received: \"%s\".",
                packet[1], nread, packet_in_hex_cstring);
                free(packet_in_hex_cstring);
        */

        /*
        // debug(1, "Realtime Audio Receiver Packet of type 0x%02X length %d received.", packet[1],
        nread);
        // now get hold of its various bits and pieces
        uint8_t version = (packet[0] & 0b11000000) >> 6;
        uint8_t padding = (packet[0] & 0b00100000) >> 5;
        uint8_t extension = (packet[0] & 0b00010000) >> 4;
        uint8_t csrc_count = packet[0] & 0b00001111;
        uint8_t marker = (packet[1] & 0b1000000) >> 7;
        uint8_t payload_type = packet[1] & 0b01111111;
        */
        // if (have_ptp_timing_information(conn)) {
        if (1) {
          int32_t seqno = decipher_player_put_packet(packet + 2, nread - 2, conn);
          if (seqno >= 0) {
            if (last_seqno == -1) {
              last_seqno = seqno;
            } else {
              last_seqno = (last_seqno + 1) & 0xffff;
              // if (seqno != last_seqno)
              //  debug(3, "RTP: Packets out of sequence: expected: %d, got %d.", last_seqno,
              //  seqno);
              last_seqno = seqno; // reset warning...
            }
          } else {
            debug(1, "Realtime Audio Receiver -- bad packet dropped.");
          }
        }
      } else {
        debug(3, "Realtime Audio Receiver -- dropping a packet.");
      }
    } else {
      debug(1, "Realtime Audio Receiver -- error receiving a packet.");
    }
  }
  pthread_cleanup_pop(0); // don't execute anything here.
  pthread_exit(NULL);
}

int frame_to_local_time(uint32_t timestamp, uint64_t *time, rtsp_conn_info *conn) {
  return frame_to_ptp_local_time(timestamp, time, conn);
}

int local_time_to_frame(uint64_t time, uint32_t *frame, rtsp_conn_info *conn) {
  return local_ptp_time_to_frame(time, frame, conn);
}

void reset_anchor_info(rtsp_conn_info *conn) {
  reset_ptp_anchor_info(conn);
}

int have_timestamp_timing_information(rtsp_conn_info *conn) {
  return have_ptp_timing_information(conn);
}

