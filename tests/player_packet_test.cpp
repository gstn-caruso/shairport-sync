#include "session_state.hpp"
#include <array>
#include <gtest/gtest.h>

static void checkMutedDecodeFailuresThrough(ssrc_t lastEncoding) {
  SessionState session{};
  ASSERT_EQ(pthread_mutex_init(&session.flush_mutex, nullptr), 0);
  std::array<uint8_t, 16> invalid{};
  const std::array formats{ALAC_44100_S16_2, AAC_48000_F24_2};
  for (uint16_t sequence = 0; sequence < formats.size(); ++sequence) {
    const auto encoding = formats[sequence];
    EXPECT_EQ(player_put_packet(encoding, sequence, 1000 + sequence, invalid.data(),
                                sequence == 0 ? 8 : invalid.size(), 1, 0, &session), 0);
    auto front = session.packetBuffer.front();
    EXPECT_TRUE(front);
    if (front) {
      EXPECT_TRUE(front->packet.ready);
      EXPECT_EQ(front->packet.frames, AudioFormat::fromSsrc(encoding)->framesPerPacket());
      EXPECT_TRUE(session.packetBuffer.takeFrontIf(front->revision));
    }
    if (encoding == lastEncoding)
      break;
  }
  EXPECT_EQ(pthread_mutex_destroy(&session.flush_mutex), 0);
}

TEST(PlayerPacket, AlacDecodeFailureThenMutePreservesPacketDuration) {
  checkMutedDecodeFailuresThrough(ALAC_44100_S16_2);
}

TEST(PlayerPacket, AacDecodeFailureAfterAlacThenMutePreservesPacketDuration) {
  checkMutedDecodeFailuresThrough(AAC_48000_F24_2);
}
