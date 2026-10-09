#include "audio_format.hpp"
#include <cassert>
#include <cstring>

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
    auto actual = AudioFormat::fromSsrc(format.ssrc);
    assert(actual);
    assert(actual->isAac() == format.aac);
    assert(actual->sampleRate() == format.rate);
    assert(actual->framesPerPacket() == format.frames);
    assert(actual->name() == format.name);
    assert(actual->ssrc() == format.ssrc);
    assert(actual->channels() == (format.ssrc == AAC_48000_F24_5P1 ? 6 :
                                  format.ssrc == AAC_48000_F24_7P1 ? 8 : 2));
    assert(actual->aacChannelConfiguration() == (format.ssrc == AAC_48000_F24_7P1 ? 7 :
                                                 actual->channels()));
    assert(actual->suggestedSampleFormat() == (format.ssrc == ALAC_44100_S16_2 ? SPS_FORMAT_S16 :
                                               format.ssrc == ALAC_48000_S24_2 ? SPS_FORMAT_S24 :
                                                                                 SPS_FORMAT_S32));
  }
  assert(!AudioFormat::fromSsrc(SSRC_NONE));
  assert(!AudioFormat::fromSsrc(static_cast<ssrc_t>(0xf00d)));
}
