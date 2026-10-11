#pragma once
#include "audio/format/audio_types.h"

// The output backend ABI packs even sample rates, channel counts, and SPS formats.
#define RATE_FROM_ENCODED_FORMAT(encoded_format) (((encoded_format >> 6) & 0x7FFFF) * 2)
#define RATE_TO_ENCODED_FORMAT(rate) (((rate / 2) & 0x7FFFF) << 6)
#define CHANNELS_FROM_ENCODED_FORMAT(encoded_format) ((encoded_format >> 25) & 0x7F)
#define CHANNELS_TO_ENCODED_FORMAT(channels) ((channels & 0x7F) << 25)
static inline sps_format_t format_from_encoded_format(uint32_t encoded_format) {
  return (sps_format_t)(encoded_format & 0x3F);
}
#define FORMAT_FROM_ENCODED_FORMAT(encoded_format) format_from_encoded_format(encoded_format)
#define FORMAT_TO_ENCODED_FORMAT(format) (format & 0x3F)
