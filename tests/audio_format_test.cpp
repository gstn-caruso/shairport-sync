#include "player.h"
#include <cassert>
#include <cstring>
uint32_t get_ssrc_rate(ssrc_t);

int main() {
  struct Expectation { ssrc_t ssrc; unsigned rate; size_t frames; bool aac; const char *name; };
  const Expectation formats[] = {
    {ALAC_44100_S16_2, 44100, 352, false, "ALAC/44100/S16_LE/2"},
    {ALAC_48000_S24_2, 48000, 352, false, "ALAC/48000/S24_LE/2"},
    {AAC_44100_F24_2, 44100, 1024, true, "AAC/44100/F24/2"},
    {AAC_48000_F24_2, 48000, 1024, true, "AAC/48000/F24/2"},
    {AAC_48000_F24_5P1, 48000, 1024, true, "AAC/48000/F24/5.1"},
    {AAC_48000_F24_7P1, 48000, 1024, true, "AAC/48000/F24/7.1"}
  };
  for (const auto &format : formats) {
    assert(ssrc_is_recognised(format.ssrc));
    assert(bool(ssrc_is_aac(format.ssrc)) == format.aac);
    assert(get_ssrc_rate(format.ssrc) == format.rate);
    assert(get_ssrc_block_length(format.ssrc) == format.frames);
    assert(std::strcmp(get_ssrc_name(format.ssrc), format.name) == 0);
  }
  assert(!ssrc_is_recognised(SSRC_NONE));
  assert(!ssrc_is_recognised(static_cast<ssrc_t>(0xf00d)));
  assert(get_ssrc_rate(SSRC_NONE) == 0);
  assert(get_ssrc_block_length(static_cast<ssrc_t>(0xf00d)) == 0);
}
