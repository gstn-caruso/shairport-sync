#include "buffered_block_fixture.hpp"
#include "session/session_state.hpp"
#include "protocol/rtp/rtp.h"
#include <gtest/gtest.h>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

struct RealtimeSubmission {
  uint32_t ssrc,timestamp;
  uint16_t sequence;
  int mute,gap;
  std::vector<uint8_t> bytes;
};
static std::mutex submittedMutex;
static std::condition_variable submittedChanged;
static std::vector<RealtimeSubmission> submitted;
extern "C" uint32_t __wrap_player_put_packet(uint32_t ssrc,seq_t sequence,uint32_t timestamp,
    uint8_t *data,size_t size,int mute,int32_t gap,rtsp_conn_info *) {
  {
    std::lock_guard lock(submittedMutex);
    submitted.push_back({ssrc,timestamp,sequence,mute,gap,{data,data+size}});
  }
  submittedChanged.notify_all();
  return 352;
}
int32_t decipher_player_put_packet(uint8_t *,ssize_t,rtsp_conn_info *);

TEST(RealtimeAudioLegacy, GoldenStrippedPacketAuthenticatesAndSubmitsExactPlaintext) {
  submitted.clear();
  SessionState session{};
  session.session_key = const_cast<uint8_t *>(buffered_block_fixture::key.data());
  auto wire = buffered_block_fixture::encrypt();
  decipher_player_put_packet(wire.data()+2,wire.size()-2,&session);
  ASSERT_EQ(submitted.size(), 1u);
  EXPECT_EQ(submitted[0].ssrc, ALAC_44100_S16_2);
  EXPECT_EQ(submitted[0].sequence, 0xcdef);
  EXPECT_EQ(submitted[0].timestamp, 0x01020304u);
  EXPECT_EQ(submitted[0].mute, 0); EXPECT_EQ(submitted[0].gap, 0);
  EXPECT_EQ(submitted[0].bytes, (std::vector<uint8_t>(buffered_block_fixture::plaintext.begin(),buffered_block_fixture::plaintext.end())));
}
TEST(RealtimeAudioLegacy, FailedAuthenticationCurrentlySubmitsAnEmptyPacket) {
  submitted.clear();
  SessionState session{};
  session.session_key = const_cast<uint8_t *>(buffered_block_fixture::key.data());
  auto wire = buffered_block_fixture::encrypt(); wire[12] ^= 1;
  decipher_player_put_packet(wire.data()+2,wire.size()-2,&session);
  ASSERT_EQ(submitted.size(), 1u);
  EXPECT_TRUE(submitted[0].bytes.empty());
}
