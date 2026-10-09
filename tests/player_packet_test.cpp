#include "session_state.hpp"
#include <array>
#include <cassert>

int main() {
  SessionState session{};
  assert(pthread_mutex_init(&session.ab_mutex, nullptr) == 0);
  assert(pthread_mutex_init(&session.flush_mutex, nullptr) == 0);
  assert(pthread_cond_init(&session.flowcontrol, nullptr) == 0);
  std::array<uint8_t, 16> invalid{};
  const std::array formats{ALAC_44100_S16_2, AAC_48000_F24_2};
  for (uint16_t sequence = 0; sequence < formats.size(); ++sequence) {
    const auto encoding = formats[sequence];
    assert(player_put_packet(encoding, sequence, 1000 + sequence, invalid.data(),
                             sequence == 0 ? 8 : invalid.size(), 1, 0, &session) == 0);
    const auto &packet = session.audio_buffer[sequence];
    assert(packet.avframe == nullptr && packet.ready);
    assert(packet.length == AudioFormat::fromSsrc(encoding)->framesPerPacket());
  }
  assert(pthread_cond_destroy(&session.flowcontrol) == 0);
  assert(pthread_mutex_destroy(&session.ab_mutex) == 0);
  assert(pthread_mutex_destroy(&session.flush_mutex) == 0);
  abuf_t pcm{};
  pcm.timestamp = 100;
  pcm.length = 4;
  pcm.data = *ConvertedAudio::allocate(16, 4, 0);
  auto *samples = reinterpret_cast<int16_t *>(pcm.data.bytes().data());
  for (int index = 0; index < 8; ++index)
    samples[index] = index + 1;
  pcm.trimBefore(102, 4);
  assert(pcm.timestamp == 102 && pcm.length == 2);
  assert(samples[0] == 5 && samples[3] == 8);
}
