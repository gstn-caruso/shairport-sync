#include "playback_samples.hpp"
#include <cassert>
#include <cstring>

int main() {
  PlaybackSamples samples([](size_t) { return size_t{1}; });
  PcmEncoder encoder([] { return int64_t{0}; });
  assert(encoder.configure({SPS_FORMAT_S32_LE, 2}, 16));
  auto audio = *ConvertedAudio::allocate(400, 100, 0, {2, 16, 16});
  for (size_t frame = 0; frame < 100; ++frame) {
    const int16_t pair[]{INT16_MIN, INT16_MAX};
    std::memcpy(audio.bytes().data() + frame * 4, pair, 4);
  }
  assert(samples.prepare(audio, PlaybackMode::stereo));
  encoder.beginFrame(0x10000, false);
  auto encoded = samples.encode(encoder, {CorrectionStyle::basic, 0});
  assert(encoded.frames() == 100 && encoded.bytes().size() == 800);
  const uint8_t expected[]{0, 0, 0, 0x80, 0, 0, 0xff, 0x7f};
  assert(std::memcmp(encoded.bytes().data(), expected, sizeof(expected)) == 0);
}
