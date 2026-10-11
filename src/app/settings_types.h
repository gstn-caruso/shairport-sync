#pragma once
#include "audio/format/audio_types.h"

typedef enum {
  SS_LITTLE_ENDIAN = 0,
  SS_PDP_ENDIAN,
  SS_BIG_ENDIAN,
} endian_type;

typedef enum {
  ST_basic = 0, // straight deletion or insertion of a frame in a 352-frame packet
  ST_vernier,   // interpolate from 352/1024 samples to 353/1025 or 351/1023
  ST_auto,      // select interpolation automatically
} stuffing_type;

typedef enum {
  ST_stereo = 0,
  ST_mono,
  ST_reverse_stereo,
  ST_left_only,
  ST_right_only,
} playback_mode_type;

typedef enum {
  VCP_standard = 0,
  VCP_flat,
  VCP_dasl_tapered,
} volume_control_profile_type;

typedef enum {
  SPS_RATE_UNKNOWN = 0,
  SPS_RATE_5512,
  SPS_RATE_LOWEST = SPS_RATE_5512,
  SPS_RATE_8000,
  SPS_RATE_11025,
  SPS_RATE_16000,
  SPS_RATE_22050,
  SPS_RATE_32000,
  SPS_RATE_44100,
  SPS_RATE_48000,
  SPS_RATE_64000,
  SPS_RATE_88200,
  SPS_RATE_96000,
  SPS_RATE_176400,
  SPS_RATE_192000,
  SPS_RATE_352800,
  SPS_RATE_384000,
  SPS_RATE_HIGHEST = SPS_RATE_384000,
  SPS_RATE_ILLEGAL,
} sps_rate_t;

// these sets omit the _UNKNOWN, _AUTO and _ILLEGAL values
#define SPS_FORMAT_SET (((1 << (SPS_FORMAT_HIGHEST_NATIVE + 1)) - 1) - (1 << SPS_FORMAT_UNKNOWN))
#define SPS_RATE_SET (((1 << (SPS_RATE_HIGHEST + 1)) - 1) - (1 << SPS_RATE_UNKNOWN))

// in SPS_CHANNEL_SET, bit 0 set means a channel set of no channels, bit 1 set means a channel set
// of 1 channel and so on to bit 31 meaning a channel set of 31 channels. We want to consider all
// possible channel sets apart from channel set 0.
#define SPS_GREATEST_CHANNEL_COUNT 31 // should be 32 to be fully in line with ALSA limits
#define SPS_CHANNEL_SET 0xFFFFFFFE    // channel sets 31 to 1, but no channel set 0
// #define SPS_CHANNEL_SET (((1 << (SPS_GREATEST_CHANNEL_COUNT + 1)) - 1) - (1 << 0)) // channels 1
// to 31, not 0-based!
