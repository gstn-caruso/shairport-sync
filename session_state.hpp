#pragma once

#include "common.h"
#include "player.h"
#include "rtp_clock.hpp"

struct SessionState {
  SessionState() = default;
  SessionState(const SessionState &) = delete;
  SessionState &operator=(const SessionState &) = delete;
  ~SessionState();
  int connection_number;           // for debug ID purposes, nothing else...
  int is_playing;                  // set true by player_play, set false by player_stop
  int input_format_is_valid;       // set when the input format is known and set in this structure
  unsigned int sync_samples_index; // for estimating the gap between the highest and lowest timing
                                   // error over the past n samples
  unsigned int sync_samples_count; // the array of samples is defined locally
  int at_least_one_frame_seen_this_session; // set when the first frame is output
  int resend_interval;                      // this is really just for debugging
  char *UserAgent;                          // free this on teardown
  int AirPlayVersion; // zero if not an AirPlay session. Used to help calculate latency
  int latency_warning_issued;
  uint32_t latency;          // the actual latency used for this play session
  uint32_t minimum_latency;  // set if an a=min-latency: line appears in the ANNOUNCE message; zero
                             // otherwise
  uint32_t maximum_latency;  // set if an a=max-latency: line appears in the ANNOUNCE message; zero
                             // otherwise
  int software_mute_enabled; // if we don't have a real mute that we can use
  int fd = -1;
  SOCKADDR remote, local;
  volatile int stop;
  volatile int running;

  uint64_t playstart;
  uint64_t connection_start_time; // the time the device is selected, which could be a long time
                                  // before a play
  pthread_t thread;

  // buffers to delete on exit
  int32_t *tbuf;
  char *outbuf;

  // for generating running statistics...

  // stats_t *statistics;

  // for holding the output rate information until printed out at the end of a session
  double raw_frame_rate;
  double corrected_frame_rate;
  int frame_rate_valid;

  // for holding input rate information until printed out at the end of a session

  double input_frame_rate;
  int input_frame_rate_starting_point_is_valid;

  uint64_t frames_inward_measurement_start_time;
  uint32_t frames_inward_frames_received_at_measurement_start_time;

  uint64_t frames_inward_measurement_time;
  uint32_t frames_inward_frames_received_at_measurement_time;

  // other stuff...
  pthread_t *player_thread;
  abuf_t audio_buffer[BUFFER_FRAMES];
  unsigned int frames_per_packet, input_num_channels, input_bit_depth, input_effective_bit_depth,
      input_rate;
  int input_bytes_per_frame;
  unsigned int output_sample_ratio;
  unsigned int output_bit_depth;
  int64_t previous_random_number;
  uint64_t packet_count;
  uint64_t packet_count_since_flush;
  // int connection_state_to_output;
  uint64_t first_packet_time_to_play;
  int64_t time_since_play_started; // nanoseconds
                                   // stats
  uint64_t missing_packets, late_packets, too_late_packets, resend_requests;
  // debug variables
  int last_seqno_valid;
  seq_t last_seqno_read;
  // mutexes and condition variables
  pthread_cond_t flowcontrol;
  pthread_mutex_t ab_mutex, flush_mutex, volume_control_mutex, player_create_delete_mutex;

  int fix_volume;
  double own_airplay_volume;
  int own_airplay_volume_set;

  int ab_buffering, ab_synced;
  uint32_t first_packet_timestamp;
  int flush_requested;
  int flush_output_flushed; // true if the output device has been flushed.
  uint32_t flush_rtp_timestamp;
  uint64_t time_of_last_audio_packet;
  seq_t ab_read, ab_write;




  int32_t framesProcessedInThisEpoch;
  int32_t framesGeneratedInThisEpoch;
  int32_t correctionsRequestedInThisEpoch;
  int64_t syncErrorsInThisEpoch;

  // RTP stuff
  // only one RTP session can be active at a time.
  int rtp_running;
  uint64_t rtp_time_of_last_resend_request_error_ns;

  char client_ip_string[INET6_ADDRSTRLEN]; // the ip string of the client
  uint16_t client_rtsp_port;
  char self_ip_string[INET6_ADDRSTRLEN]; // the ip string being used by this program -- it
  uint16_t self_rtsp_port;               // could be one of many, so we need to know it

  uint32_t self_scope_id;     // if it's an ipv6 connection, this will be its scope
  short connection_ip_family; // AF_INET / AF_INET6


  int64_t latency_delayed_timestamp; // this is for debugging only...

  // this is what connects an rtp timestamp to the remote time

  int udp_clock_is_initialised;
  int udp_clock_sender_is_initialised;

  RtpClock clock;

  airplay_stream_c
      airplay_stream_category; // is it a remote control stream or a normal "full service" stream?

  plist_t sessionPlist;
  char *airplay_gid; // UUID in the Bonjour advertisement -- if NULL, the group UUID is the same as
                     // the pi UUID
  airplay_stream_t airplay_stream_type; // is it realtime audio or buffered audio...

  pthread_t *rtp_event_thread;
  pthread_t *rtp_data_thread;
  pthread_t rtp_ap2_control_thread;
  pthread_t rtp_realtime_audio_thread;
  pthread_t rtp_buffered_audio_thread;
  pthread_mutex_t event_sender_mutex;
  int event_channel_fd; // zero if closed, controlled by event_sender_mutex

  int ap2_event_receiver_exited;

  int ap2_immediate_flush_requested;
  uint32_t ap2_immediate_flush_until_rtp_timestamp;
  uint32_t ap2_immediate_flush_until_sequence_number;

  ap2_flush_request_t ap2_deferred_flush_requests[MAX_DEFERRED_FLUSH_REQUESTS];

  ssize_t ap2_audio_buffer_size;
  ssize_t ap2_audio_buffer_minimum_size;

  int ap2_rate;         // protect with flush mutex, 0 means don't play, 1 means play
  int ap2_play_enabled; // protect with flush mutex

  ap2_pairing ap2_pairing_context;
  struct pair_result *pair_setup_result; // need to keep the shared secret

  int event_socket;
  int data_socket;
  SOCKADDR ap2_remote_control_socket_addr; // a socket pointing to the control port of the client
  socklen_t ap2_remote_control_socket_addr_length;
  int ap2_control_socket;
  int realtime_audio_socket;
  int buffered_audio_socket;

  uint16_t local_data_port;
  uint16_t local_event_port;
  uint16_t local_ap2_control_port;
  uint16_t local_realtime_audio_port;
  uint16_t local_buffered_audio_port;

  uint64_t audio_format;
  uint64_t compression;
  unsigned char *session_key; // needs to be free'd at the end
  char *ap2_client_name;      // needs to be free'd at teardown phase 2
  uint64_t frames_packet;
  uint64_t type;                  // 96 (Realtime Audio), 103 (Buffered Audio), 130 (Remote Control)
  uint64_t networkTimeTimelineID; // the clock ID used by the player
  uint8_t groupContainsGroupLeader; // information coming from the SETUP
  uint64_t compressionType;

  ssrc_t incoming_ssrc;  // The SSRC of incoming packets. In AirPlay 2, the RTP SSRC seems to encode
                         // something about the contents of the packet -- Atmos/etc. We use it also
                         // even in AP1 as a code
  ssrc_t resampler_ssrc; // the SSRC of packets for which the software resampler has been set up.
  // normally it's the same as that of incoming packets, but if the encoding of incoming packets
  // changes dynamically and the decoding chain hasn't been reset, the resampler will have to deal
  // with queued AVFrames encoded according to the previous SSRC.
  const AVCodec *codec;
  AVCodecContext *codec_context;
  // the swr can't be used just after the incoming packet has been decoded as explained below

  // The reasons that resampling can not occur when the packet initially arrives are twofold.
  // Resampling requires input samples from before and after the resampling instant.
  // So, at the end of a block, since the subsequent samples aren't in the block, resampling
  // is deferred until the next block is loaded. From this, the two reasons follow:
  // First, the "next" block to be provided in player_put_packet is not guaranteed to be
  // the next block in sequence -- packets can arrive out of sequence in UDP transmission.
  // Second, not all the frames that should be generated for a block will be generated
  // by a call to swr_convert. The frames that can't be calculated will not be provided, and
  // will be held back and provided to the subsequent call.
  // Tht means that the first frame output by swr_convert will not in general,
  // not correspond to the first frame provided to it, throwing
  // timing calculations off.

  // In summary, we have to wait until (1) we have all the blocks in order,
  // and (2) we have to track the number of resampler output frames to
  // keep the correspondence between them and the input frames.

  // We can calculate the "deficit" between the number of frames that should be generated
  // versus the number of frames actually generated.

  // For example, converting 352 frames at 44,100 to 48,000 should result
  // in 352 * 48000 / 44100, or 383.129252 frames.

  // Say only 360 frames are actually produced, then the deficit is 23.129252.

  // If those 360 frames are sent to the output device, then the timing of the next
  // block of 352 frames will be ahead by 23.129252 frames at 48,000 fps -- about 0.48 ms.

  // We need to add the delay corresponding to the frames that should have been sent to
  // keep timing correct. I.e. when calculating the buffer delay at the start of the following
  // block, those 23.129252 frames the were not actually sent should be added to it.

  // The "deficit" can readily be kept up to date and can be always added to the
  // DAC buffer delay to exactly compensate for the

  SwrContext *swr; // this will do transcoding anf resampling, if necessary, just prior to output
  int ffmpeg_decoding_chain_initialised;
  int64_t resampler_output_channels;
  int resampler_output_bytes_per_sample;
  int64_t frames_retained_in_the_resampler; // swr will retain frames it hasn't finished processing
  // they'll come out before the frames corresponding to the start of next block passed to
  // swrconvert so we need to compensate for their absence in sync timing
  unsigned int output_channel_to_resampler_channel_map[8];
  unsigned int output_channel_map_size;

  // used as the initials values for calculating the rate at which the source thinks it's sending
  // frames
  uint32_t initial_reference_timestamp;
  uint64_t initial_reference_time;
  double remote_frame_rate;

  // the ratio of the following should give us the operating rate, nominally 44,100
  int64_t reference_to_previous_frame_difference;
  uint64_t reference_to_previous_time_difference;

  // debug variables
  int request_sent;

  pthread_mutex_t reference_time_mutex;

  int last_stuff_request;

  // int64_t play_segment_reference_frame;
  // uint64_t play_segment_reference_frame_remote_time;

  int32_t buffer_occupancy; // allow it to be negative because seq_diff may be negative
  int64_t session_corrections;

  int play_number_after_flush;

  // remote control stuff. The port to which to send commands is not specified, so you have to use
  // mdns to find it.
  // at present, only avahi can do this

  char *dacp_id; // id of the client -- used to find the port to be used
  //  uint16_t dacp_port;          // port on the client to send remote control messages to, else
  //  zero
  char *dacp_active_remote;   // key to send to the remote controller
  void *dapo_private_storage; // this is used for compatibility, if dacp stuff isn't enabled.

  int enable_dither; // needed for filling silences before play actually starts
};
