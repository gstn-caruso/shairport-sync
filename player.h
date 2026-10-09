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

// clang-format off

/*
__________________________________________________________________________________________________________________________________
* ALAC Specific Info (24 bytes) (mandatory)
__________________________________________________________________________________________________________________________________

The Apple Lossless codec stores specific information about the encoded stream in the ALACSpecificConfig. This
info is vended by the encoder and is used to setup the decoder for a given encoded bitstream.

When read from and written to a file, the fields of this struct must be in big-endian order.
When vended by the encoder (and received by the decoder) the struct values will be in big-endian order.

    struct	ALACSpecificConfig (defined in ALACAudioTypes.h)
    abstract   	This struct is used to describe codec provided information about the encoded Apple Lossless bitstream.
		It must accompany the encoded stream in the containing audio file and be provided to the decoder.

    field      	frameLength 		uint32_t	indicating the frames per packet when no explicit frames per packet setting is
							present in the packet header. The encoder frames per packet can be explicitly set
							but for maximum compatibility, the default encoder setting of 4096 should be used.

    field      	compatibleVersion 	uint8_t 	indicating compatible version,
							value must be set to 0

    field      	bitDepth 		uint8_t 	describes the bit depth of the source PCM data (maximum value = 32)

    field      	pb 			uint8_t 	currently unused tuning parameter.
						 	value should be set to 40

    field      	mb 			uint8_t 	currently unused tuning parameter.
						 	value should be set to 10

    field      	kb			uint8_t 	currently unused tuning parameter.
						 	value should be set to 14

    field      	numChannels 		uint8_t 	describes the channel count (1 = mono, 2 = stereo, etc...)
							when channel layout info is not provided in the 'magic cookie', a channel count > 2
							describes a set of discreet channels with no specific ordering

    field      	maxRun			uint16_t 	currently unused.
   						  	value should be set to 255

    field      	maxFrameBytes 		uint32_t 	the maximum size of an Apple Lossless packet within the encoded stream.
						  	value of 0 indicates unknown

    field      	avgBitRate 		uint32_t 	the average bit rate in bits per second of the Apple Lossless stream.
						  	value of 0 indicates unknown

    field      	sampleRate 		uint32_t 	sample rate of the encoded stream
 */

// clang-format on

typedef struct __attribute__((__packed__)) ALACSpecificConfig {
  uint32_t frameLength;
  uint8_t compatibleVersion;
  uint8_t bitDepth;
  uint8_t pb;
  uint8_t mb;
  uint8_t kb;
  uint8_t numChannels;
  uint16_t maxRun;
  uint32_t maxFrameBytes;
  uint32_t avgBitRate;
  uint32_t sampleRate;

} ALACSpecificConfig;

// everything in here is big-endian, i.e. network byte order
typedef struct __attribute__((__packed__)) alac_ffmpeg_magic_cookie {
  uint32_t cookie_size;    // 36 bytes
  uint32_t cookie_tag;     // 'alac'
  uint32_t cookie_version; // 0
  ALACSpecificConfig alac_config;
} alac_ffmpeg_magic_cookie;


// these are for reporting the status of the clock
typedef uint16_t seq_t;

// these are the values coming in on a buffered audio RTP packet's SSRC field
// the apparent significances are as indicated.
// Dolby Atmos seems to be 7P1
typedef enum ssrc_type
#ifdef __cplusplus
    : uint32_t
#endif
{
  SSRC_NONE = 0,
  ALAC_44100_S16_2 = 0x0000FACE, // this is made up
  ALAC_48000_S24_2 = 0x15000000,
  AAC_44100_F24_2 = 0x16000000,
  AAC_48000_F24_2 = 0x17000000,
  AAC_48000_F24_5P1 = 0x27000000,
  AAC_48000_F24_7P1 = 0x28000000,
} ssrc_t;

typedef struct audio_buffer_entry { // decoded audio packets
  uint8_t ready;
  uint8_t status; // flags
  uint16_t resend_request_number;
  signed short *data;
  seq_t sequence_number;
  uint64_t initialisation_time; // the time the packet was added or the time it was noticed the
                                // packet was missing
  uint64_t resend_time;         // time of last resend request or zero
  uint32_t timestamp;           // for timing
  int32_t timestamp_gap;        // the difference between the timestamp and the expected timestamp.
  size_t length; // the length of the decoded data (or silence requested) in input frames
  ssrc_t ssrc;      // this is the type of this specific frame.
  AVFrame *avframe; // Decoded audio carried by FFmpeg before output conversion.
} abuf_t;

typedef struct stats { // statistics for running averages
  uint32_t timestamp;  // timestamp (denominated in input frames)
  size_t frames;       // number of audio frames in the block (denominated in output frames)
  int64_t sync_error, correction, drift;
} stats_t;

// default buffer size
// This needs to be a power of 2 because of the way BUFIDX(seqno) works.
// 512 is the minimum for normal operation -- it gives 512*352/44100 or just over 4 seconds of
// buffers.
// For at least 10 seconds, you need to go to 2048.
// Resend requests will be spaced out evenly in the latency period, subject to a minimum interval of
// about 0.25 seconds.
// Each buffer occupies 352*4 bytes plus about, say, 64 bytes of overhead in various places, say
// roughly 1,500 bytes per buffer.
// Thus, 2048 buffers will occupy about 3 megabytes -- no big deal in a normal machine but maybe a
// problem in an embedded device.

#define BUFFER_FRAMES 1024

// maximum number of frames that can be added or removed from a packet_count
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

typedef struct {
  struct pair_cipher_context *cipher_ctx;
  sized_buffer encrypted_read_buffer;
  sized_buffer plaintext_read_buffer;
  int is_encrypted;
  char *description;
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

extern int statistics_row; // will be reset to zero when debug level changes or statistics enabled

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
