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
  for (const auto &test : cases) {
    for (bool negative : {false, true}) {
      const auto &expected = negative ? test.negative : test.positive;
      assert(encoder.configure({test.format, 1}, 0));
      encoder.beginFrame(0x10000, false);
      encoder.appendSample(negative ? -0x12345678 : 0x12345678);
      auto owned = encoder.finishFrame();
      assert(owned.frames() == 1);
      assert(std::equal(owned.bytes().begin(), owned.bytes().end(),
                        expected.begin(), expected.end()));
    }
    assert(encoder.configure({test.format, 2}, 0));
    auto silence = encoder.silence(1);
    assert(silence.frames() == 1);
    for (auto byte : silence.bytes())
      assert(byte == (test.format == SPS_FORMAT_U8 ? 128 : 0));
  }
  for (auto format : {SPS_FORMAT_S16, SPS_FORMAT_S24, SPS_FORMAT_S32}) {
    assert(encoder.configure({format, 1}, 0));
    encoder.beginFrame(0x10000, false);
    encoder.appendSample(-0x12345678);
    auto bytes = encoder.finishFrame();
    if (format == SPS_FORMAT_S16) {
      int16_t value;
      std::memcpy(&value, bytes.bytes().data(), sizeof(value));
      assert(value == -0x1235);
    } else {
      int32_t value;
      std::memcpy(&value, bytes.bytes().data(), sizeof(value));
      assert(value == (format == SPS_FORMAT_S24 ? -0x123457 : -0x12345678));
    }
  }
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
  assert(encoder.configure({SPS_FORMAT_S16_LE, 1}, 16));
  encoder.beginFrame(0x8000, false);
  encoder.appendSample(0x12345678);
  auto attenuated = encoder.finishFrame();
  assert(attenuated.bytes()[0] == 0x1a && attenuated.bytes()[1] == 0x09);
}
