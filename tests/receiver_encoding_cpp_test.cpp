#include "common.h"
#include "player.h"
#include <cassert>
#include <cstring>

constexpr sps_format_t unsupported_format = static_cast<sps_format_t>(63);
constexpr ssrc_t unsupported_stream = static_cast<ssrc_t>(UINT32_MAX);

int main() {
  static_assert(sizeof(ssrc_t) == sizeof(uint32_t));
  static_assert(sizeof(sps_format_t) == sizeof(uint32_t));
  assert(FORMAT_FROM_ENCODED_FORMAT(63) == unsupported_format);
  assert(sps_format_sample_size(unsupported_format) == 0);
  assert(std::strcmp(sps_format_description_string(unsupported_format), "invalid") == 0);
  assert(sps_format_sample_size(FORMAT_FROM_ENCODED_FORMAT(SPS_FORMAT_S16_LE)) == 2);
  assert(ssrc_is_recognised(unsupported_stream) == 0);
}
