#include "session_state.hpp"
#include "audio_player_adapter.hpp"
#include "pcm_encoder.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <vector>

int main() {
  PcmEncoder encoder([] { return int64_t{0}; });
  assert(encoder.configure({SPS_FORMAT_S16_LE, 2}, 16));
  encoder.beginFrame(0x10000, false);
  encoder.appendSample(0x12345678);
  encoder.appendSample(-0x12345678);
  auto encoded = encoder.finishFrame();
  assert(encoded.frames() == 1);
  const std::array<uint8_t, 4> expected{0x34, 0x12, 0xcb, 0xed};
  assert(std::equal(encoded.bytes().begin(), encoded.bytes().end(), expected.begin(), expected.end()));
  struct Case { sps_format_t format; std::vector<uint8_t> positive, negative; };
  const std::array cases{
    Case{SPS_FORMAT_S8, {0x12}, {0xed}},
    Case{SPS_FORMAT_U8, {0x92}, {0x6d}},
    Case{SPS_FORMAT_S16_LE, {0x34,0x12}, {0xcb,0xed}},
    Case{SPS_FORMAT_S16_BE, {0x12,0x34}, {0xed,0xcb}},
    Case{SPS_FORMAT_S24_3LE, {0x56,0x34,0x12}, {0xa9,0xcb,0xed}},
    Case{SPS_FORMAT_S24_3BE, {0x12,0x34,0x56}, {0xed,0xcb,0xa9}},
    Case{SPS_FORMAT_S24_LE, {0x56,0x34,0x12,0}, {0xa9,0xcb,0xed,0}},
    Case{SPS_FORMAT_S24_BE, {0,0x12,0x34,0x56}, {0,0xed,0xcb,0xa9}},
    Case{SPS_FORMAT_S32_LE, {0x78,0x56,0x34,0x12}, {0x88,0xa9,0xcb,0xed}},
    Case{SPS_FORMAT_S32_BE, {0x12,0x34,0x56,0x78}, {0xed,0xcb,0xa9,0x88}}
  };
  SessionState session{};
  for (const auto &test : cases) {
    for (bool negative : {false, true}) {
      std::array<char, 4> bytes{};
      char *next = bytes.data();
      encodePlaybackSample(negative ? -0x12345678 : 0x12345678, &next, test.format,
                           0x10000, 0, &session);
      const auto &expected = negative ? test.negative : test.positive;
      assert(next == bytes.data() + expected.size());
      assert(std::memcmp(bytes.data(), expected.data(), expected.size()) == 0);
      assert(encoder.configure({test.format, 1}, 0));
      encoder.beginFrame(0x10000, false);
      encoder.appendSample(negative ? -0x12345678 : 0x12345678);
      auto owned = encoder.finishFrame();
      assert(owned.frames() == 1);
      assert(std::equal(owned.bytes().begin(), owned.bytes().end(),
                        expected.begin(), expected.end()));
    }
    std::array<char, 8> silence{};
    r64init(123);
    const auto configuration = CHANNELS_TO_ENCODED_FORMAT(2) | FORMAT_TO_ENCODED_FORMAT(test.format);
    generate_zero_frames(silence.data(), 1, 0, 0, configuration);
    for (size_t byte = 0; byte < test.positive.size() * 2; ++byte)
      assert(static_cast<uint8_t>(silence[byte]) == (test.format == SPS_FORMAT_U8 ? 128 : 0));
    assert(encoder.configure({test.format, 2}, 0));
    auto ownedSilence = encoder.silence(1);
    assert(ownedSilence.frames() == 1);
    assert(std::memcmp(ownedSilence.bytes().data(), silence.data(), ownedSilence.bytes().size()) == 0);
  }
  for (auto format : {SPS_FORMAT_S16, SPS_FORMAT_S24, SPS_FORMAT_S32}) {
    alignas(int32_t) std::array<char, 4> bytes{};
    char *next = bytes.data();
    encodePlaybackSample(-0x12345678, &next, format, 0x10000, 0, &session);
    if (format == SPS_FORMAT_S16) {
      int16_t value;
      std::memcpy(&value, bytes.data(), sizeof(value));
      assert(value == -0x1235);
    } else {
      int32_t value;
      std::memcpy(&value, bytes.data(), sizeof(value));
      assert(value == (format == SPS_FORMAT_S24 ? -0x123457 : -0x12345678));
    }
  }
  std::array<char, 8> mute{};
  session.enable_dither = 1;
  session.previous_random_number = 17;
  const auto configuration = CHANNELS_TO_ENCODED_FORMAT(2) | FORMAT_TO_ENCODED_FORMAT(SPS_FORMAT_S16_LE);
  r64init(456);
  const auto lastRandom = generate_zero_frames(mute.data(), 2, 1, 17, configuration);
  r64init(456);
  mutePlaybackPcm(mute.data(), 2, configuration, session);
  assert(lastRandom != 17 && session.previous_random_number == lastRandom);
  int calls = 0;
  PcmEncoder deterministic([&] {
    return (++calls % 2) ? (int64_t{1} << 48) - 1 : int64_t{0};
  });
  assert(deterministic.configure({SPS_FORMAT_S16_LE, 1}, 32));
  deterministic.beginFrame(0x10000, false);
  assert(deterministic.dithers());
  deterministic.appendSample(INT32_MAX);
  deterministic.appendSample(INT32_MIN);
  auto extremes = deterministic.finishFrame();
  const std::array<uint8_t, 4> clipped{0xff, 0x7f, 0, 0x80};
  assert(std::equal(extremes.bytes().begin(), extremes.bytes().end(), clipped.begin(), clipped.end()));
  auto betweenFrames = deterministic.silence(1);
  assert(betweenFrames.bytes()[0] == 0 && betweenFrames.bytes()[1] == 0);
  deterministic.beginFrame(0x10000, false);
  deterministic.appendSample(0);
  auto afterSilence = deterministic.finishFrame();
  assert(afterSilence.bytes()[0] == 0xff && afterSilence.bytes()[1] == 0xff);
  assert(calls == 4);
  assert(deterministic.configure({SPS_FORMAT_S16_LE, 1}, 16));
  deterministic.beginFrame(0x10000, false);
  assert(!deterministic.dithers());
  deterministic.appendSample(0);
  assert(calls == 4);
  deterministic.finishFrame();
  deterministic.silence(1, DitherPolicy::disabled);
  assert(calls == 5);
  deterministic.beginFrame(0x8000, false);
  assert(deterministic.dithers());
  deterministic.beginFrame(0x10000, true);
  assert(deterministic.dithers());
}
