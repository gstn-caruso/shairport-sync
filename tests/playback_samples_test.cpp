#include "playback_samples.hpp"
#include <cassert>
#include <cstring>
#include <bit>
#include <vector>

static int32_t sampleAt(const EncodedPcm &audio, size_t sample) {
  const auto bytes = audio.bytes().subspan(sample * 4, 4);
  const uint32_t value = uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 |
                         uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
  return std::bit_cast<int32_t>(value);
}
template <typename Sample>
static ConvertedAudio nativeAudio(std::vector<Sample> values, unsigned channels,
                                  unsigned effectiveBits) {
  auto audio = *ConvertedAudio::allocate(values.size() * sizeof(Sample), values.size() / channels,
                                         0, {channels, sizeof(Sample) * 8, effectiveBits});
  std::memcpy(audio.bytes().data(), values.data(), values.size() * sizeof(Sample));
  return audio;
}
static EncodedPcm encode(PlaybackSamples &samples, PcmEncoder &encoder,
                         const ConvertedAudio &audio, PlaybackMode mode, Correction correction) {
  assert(samples.prepare(audio, mode));
  assert(encoder.configure({SPS_FORMAT_S32_LE, audio.shape().channels()}, audio.shape().effectiveBits()));
  encoder.beginFrame(0x10000, mode == PlaybackMode::mono);
  return samples.encode(encoder, correction);
}

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
  const std::array modes{PlaybackMode::stereo, PlaybackMode::mono, PlaybackMode::reverse,
                         PlaybackMode::left, PlaybackMode::right};
  const std::array<std::array<int32_t, 2>, 5> expected16{{
    {INT32_MIN, 2147418112}, {-32768, -32768}, {2147418112, INT32_MIN},
    {INT32_MIN, INT32_MIN}, {2147418112, 2147418112}
  }};
  std::vector<int16_t> stereo16(200);
  std::vector<int32_t> stereo32(200);
  for (size_t frame = 0; frame < 100; ++frame) {
    stereo16[frame * 2] = INT16_MIN;
    stereo16[frame * 2 + 1] = INT16_MAX;
    stereo32[frame * 2] = -3;
    stereo32[frame * 2 + 1] = 2;
  }
  const std::array<std::array<int32_t, 2>, 5> expected32{{
    {-3, 2}, {-1, -1}, {2, -3}, {-3, -3}, {2, 2}
  }};
  for (size_t mode = 0; mode < modes.size(); ++mode) {
    auto from16 = nativeAudio(stereo16, 2, 16);
    auto result16 = encode(samples, encoder, from16, modes[mode], {CorrectionStyle::basic, 0});
    assert(sampleAt(result16, 0) == expected16[mode][0]);
    assert(sampleAt(result16, 1) == expected16[mode][1]);
    auto from32 = nativeAudio(stereo32, 2, 32);
    auto result32 = encode(samples, encoder, from32, modes[mode], {CorrectionStyle::basic, 0});
    assert(sampleAt(result32, 0) == expected32[mode][0]);
    assert(sampleAt(result32, 1) == expected32[mode][1]);
  }
  for (unsigned channels : {1U, 6U, 8U}) {
    std::vector<int16_t> input(100 * channels);
    for (size_t index = 0; index < input.size(); ++index)
      input[index] = static_cast<int16_t>(int(index % channels) - 3);
    auto audio = nativeAudio(input, channels, 16);
    for (auto mode : modes) {
      auto result = encode(samples, encoder, audio, mode, {CorrectionStyle::basic, 0});
      assert(result.frames() == 100);
      for (unsigned channel = 0; channel < channels; ++channel)
        assert(sampleAt(result, channel) == int32_t(input[channel]) * 65536);
    }
  }
  ConvertedAudio empty;
  assert(samples.prepare(empty, PlaybackMode::stereo));
  assert(samples.encode(encoder, {CorrectionStyle::basic, 1}).bytes().empty());
  auto incoherent = *ConvertedAudio::allocate(3, 100, 0, {2, 16, 16});
  assert(!samples.prepare(incoherent, PlaybackMode::stereo));
  assert(samples.encode(encoder, {CorrectionStyle::basic, 1}).bytes().empty());
  std::vector<int32_t> ramp(200);
  for (size_t frame = 0; frame < 100; ++frame) {
    ramp[frame * 2] = static_cast<int32_t>(frame * 100);
    ramp[frame * 2 + 1] = static_cast<int32_t>(10000 + frame * 100);
  }
  auto rampAudio = nativeAudio(ramp, 2, 32);
  for (size_t selectedAt : {size_t{1}, size_t{98}}) {
    unsigned choices = 0;
    PlaybackSamples selected([&](size_t frames) {
      assert(frames == 100);
      ++choices;
      return selectedAt;
    });
    auto inserted = encode(selected, encoder, rampAudio, PlaybackMode::stereo,
                            {CorrectionStyle::basic, 1});
    assert(inserted.frames() == 101);
    const int32_t expectedMean = selectedAt == 1 ? 50 : 9750;
    assert(sampleAt(inserted, selectedAt * 2) == expectedMean);
    assert(sampleAt(inserted, selectedAt * 2 + 1) == expectedMean + 10000);
    assert(sampleAt(inserted, 0) == 0 && sampleAt(inserted, 200) == 9900);
    auto removed = encode(selected, encoder, rampAudio, PlaybackMode::stereo,
                           {CorrectionStyle::basic, -1});
    assert(removed.frames() == 99 && sampleAt(removed, 196) == 9900);
    encode(selected, encoder, rampAudio, PlaybackMode::stereo, {CorrectionStyle::basic, 0});
    assert(choices == 2);
  }
  for (size_t frames : {size_t{99}, size_t{100}}) {
    auto constant = nativeAudio(std::vector<int32_t>(frames * 2, 1000), 2, 32);
    for (int delta : {-20, -1, 0, 1, 20}) {
      auto corrected = encode(samples, encoder, constant, PlaybackMode::stereo,
                               {CorrectionStyle::vernier, delta});
      assert(corrected.frames() == static_cast<size_t>(int(frames) + (frames == 99 ? 0 : delta)));
      assert(sampleAt(corrected, 0) == 1000);
      assert(sampleAt(corrected, corrected.frames() * 2 - 1) == 1000);
    }
  }
}
