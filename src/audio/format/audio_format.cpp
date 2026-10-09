#include "audio/format/audio_format.hpp"
#include <algorithm>
#include <array>

std::optional<AudioFormat> AudioFormat::fromSsrc(ssrc_t ssrc) {
  static const std::array formats{
      AudioFormat{ALAC_44100_S16_2, false, 44100, 2, SPS_FORMAT_S16, "ALAC/44100/S16_LE/2"},
      AudioFormat{ALAC_48000_S24_2, false, 48000, 2, SPS_FORMAT_S24, "ALAC/48000/S24_LE/2"},
      AudioFormat{AAC_44100_F24_2, true, 44100, 2, SPS_FORMAT_S32, "AAC/44100/F24/2"},
      AudioFormat{AAC_48000_F24_2, true, 48000, 2, SPS_FORMAT_S32, "AAC/48000/F24/2"},
      AudioFormat{AAC_48000_F24_5P1, true, 48000, 6, SPS_FORMAT_S32, "AAC/48000/F24/5.1"},
      AudioFormat{AAC_48000_F24_7P1, true, 48000, 8, SPS_FORMAT_S32, "AAC/48000/F24/7.1"}};
  auto found = std::ranges::find_if(formats, [ssrc](const auto &format) {
    return format.ssrc() == ssrc;
  });
  return found == formats.end() ? std::nullopt : std::optional(*found);
}
