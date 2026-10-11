#include "audio/format/native_format.hpp"

const char *sps_format_description_string_array[] = {
    "unknown", "S8",     "U8",     "S16_LE", "S16_BE", "S24_LE", "S24_BE", "S24_3LE",
    "S24_3BE", "S32_LE", "S32_BE", "S16",    "S24",    "S32",    "auto",   "invalid"};

const char *sps_format_description_string(sps_format_t format) {
  if (format <= SPS_FORMAT_AUTO)
    return sps_format_description_string_array[format];
  else
    return sps_format_description_string_array[SPS_FORMAT_INVALID];
}

unsigned int sps_rate_actual_rate(sps_rate_t rate) {
  unsigned int response = 0;
  switch (rate) {
  case SPS_RATE_5512:
    response = 5512;
    break;
  case SPS_RATE_8000:
    response = 8000;
    break;
  case SPS_RATE_11025:
    response = 11025;
    break;
  case SPS_RATE_16000:
    response = 16000;
    break;
  case SPS_RATE_22050:
    response = 22050;
    break;
  case SPS_RATE_32000:
    response = 32000;
    break;
  case SPS_RATE_44100:
    response = 44100;
    break;
  case SPS_RATE_48000:
    response = 48000;
    break;
  case SPS_RATE_64000:
    response = 64000;
    break;
  case SPS_RATE_88200:
    response = 88200;
    break;
  case SPS_RATE_96000:
    response = 96000;
    break;
  case SPS_RATE_176400:
    response = 176400;
    break;
  case SPS_RATE_192000:
    response = 192000;
    break;
  case SPS_RATE_352800:
    response = 352800;
    break;
  case SPS_RATE_384000:
    response = 384000;
    break;
  default:
    break;
  }
  return response;
}
