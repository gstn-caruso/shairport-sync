#ifndef _PLAYER_H
#define _PLAYER_H

#include <arpa/inet.h>
#include <pthread.h>

#include "config.h"
#include "definitions.h"
#include "clock_status.h"



#define MAX_DEFERRED_FLUSH_REQUESTS 10
#include "utilities/pairing_api.h"
#include <plist/plist.h>

#include "utilities/ffmpeg_api.h"

#include "audio.h"



// these are for reporting the status of the clock
typedef uint16_t seq_t;

// these are the values coming in on a buffered audio RTP packet's SSRC field
// the apparent significances are as indicated.
// Dolby Atmos seems to be 7P1
#include "audio_types.h"


// maximum number of frames that can be added or removed from a packet_count
#define BUFFER_FRAMES 1024
#define INTERPOLATION_LIMIT 20


typedef enum {
  unspecified_stream_category = 0,
  ptp_stream,
  remote_control_stream,
} airplay_stream_c; // "c" for category

typedef enum { realtime_stream, buffered_stream } airplay_stream_t;

typedef struct {
  uint8_t *data;
  size_t length;
  size_t size;
} sized_buffer;

typedef struct pair_cipher_bundle {
  struct pair_cipher_context *cipher_ctx;
  sized_buffer encrypted_read_buffer;
  sized_buffer plaintext_read_buffer;
  int is_encrypted;
  char *description;
#ifdef __cplusplus
  void release();
#endif
} pair_cipher_bundle; // cipher context and buffers

typedef struct {
  struct pair_setup_context *setup_ctx;
  struct pair_verify_context *verify_ctx;
  pair_cipher_bundle control_cipher_bundle;
  pair_cipher_bundle event_cipher_bundle;
  pair_cipher_bundle data_cipher_bundle;
  char *data_cipher_salt;
} ap2_pairing;

typedef struct {
  uint32_t inUse;  // record free or contains a current flush record
  uint32_t active; // set if blocks within the given range are being flushed.
  uint32_t flushFromTS;
  uint32_t flushFromSeq;
  uint32_t flushUntilTS;
  uint32_t flushUntilSeq;
} ap2_flush_request_t;


#ifdef __cplusplus
struct SessionState;
using rtsp_conn_info = SessionState;
#else
typedef struct rtsp_conn_info rtsp_conn_info;
#endif


#ifdef __cplusplus
extern "C" {
#endif


void reset_buffer(rtsp_conn_info *conn);

size_t get_audio_buffer_occupancy(rtsp_conn_info *conn);

int32_t modulo_32_offset(uint32_t from, uint32_t to);

void ab_resync(rtsp_conn_info *conn);

int player_play(rtsp_conn_info *conn);
int player_stop(rtsp_conn_info *conn);

void player_volume(double f, rtsp_conn_info *conn);
void player_volume_without_notification(double f, rtsp_conn_info *conn);
void player_flush(uint32_t timestamp, rtsp_conn_info *conn);
// void player_full_flush(rtsp_conn_info *conn);

seq_t get_revised_seqno(rtsp_conn_info *conn, uint32_t timestamp);
void clear_buffers_from(rtsp_conn_info *conn, seq_t from_here);
uint32_t player_put_packet(uint32_t ssrc, seq_t seqno, uint32_t actual_timestamp, uint8_t *data,
                           size_t len, int mute, int32_t timestamp_gap, rtsp_conn_info *conn);
int64_t monotonic_timestamp(uint32_t timestamp,
                            rtsp_conn_info *conn); // add an epoch to the timestamp. The monotonic
// timestamp guaranteed to start between 2^32 2^33
// frames and continue up to 2^64 frames
// which is about 2*10^8 * 1,000 seconds at 384,000 frames per second -- about 2 trillion seconds.
// assumes, without checking, that successive timestamps in a series always span an interval of less
// than one minute.

double suggested_volume(rtsp_conn_info *conn); // volume suggested for the connection

const char *get_ssrc_name(ssrc_t ssrc);
size_t get_ssrc_block_length(ssrc_t ssrc);

const char *get_category_string(airplay_stream_c cat);

int ssrc_is_recognised(ssrc_t ssrc);
int ssrc_is_aac(ssrc_t ssrc); // used to decide if a mute might be needed (AAC only)
void prepare_decoding_chain(rtsp_conn_info *conn, ssrc_t ssrc); // also sets up timing stuff
void clear_decoding_chain(rtsp_conn_info *conn);                // tear down the decoding chain
AVFrame *block_to_avframe(rtsp_conn_info *conn, uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif //_PLAYER_H
