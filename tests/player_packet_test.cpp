#include "session_state.hpp"
#include <array>
#include <cassert>

int main() {
  SessionState session{};
  assert(pthread_mutex_init(&session.flush_mutex, nullptr) == 0);
  std::array<uint8_t, 16> invalid{};
  const std::array formats{ALAC_44100_S16_2, AAC_48000_F24_2};
  for (uint16_t sequence = 0; sequence < formats.size(); ++sequence) {
    const auto encoding = formats[sequence];
    assert(player_put_packet(encoding, sequence, 1000 + sequence, invalid.data(),
                             sequence == 0 ? 8 : invalid.size(), 1, 0, &session) == 0);
    auto front = session.packetBuffer.front();
    assert(front && front->packet.ready);
    assert(front->packet.frames == AudioFormat::fromSsrc(encoding)->framesPerPacket());
    assert(session.packetBuffer.takeFrontIf(front->revision));
  }
  assert(pthread_mutex_destroy(&session.flush_mutex) == 0);
}
