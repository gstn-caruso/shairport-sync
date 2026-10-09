#include "session_state.hpp"
#include "audio_player_adapter.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <vector>

int main() {
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
    }
    std::array<char, 8> silence{};
    r64init(123);
    const auto configuration = CHANNELS_TO_ENCODED_FORMAT(2) | FORMAT_TO_ENCODED_FORMAT(test.format);
    generate_zero_frames(silence.data(), 1, 0, 0, configuration);
    for (size_t byte = 0; byte < test.positive.size() * 2; ++byte)
      assert(static_cast<uint8_t>(silence[byte]) == (test.format == SPS_FORMAT_U8 ? 128 : 0));
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
  assert(lastRandom != 17 && session.previous_random_number == 17);
}
