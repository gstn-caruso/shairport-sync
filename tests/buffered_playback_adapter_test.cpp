#include "buffered_block_fixture.hpp"
#include "session/session_state.hpp"
#include "protocol/rtp/rtp.h"
#include <gtest/gtest.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

struct Submission {
  uint16_t sequence;
  uint32_t timestamp;
  int mute;
  int32_t gap;
  std::vector<uint8_t> payload;
};
static std::vector<Submission> submitted;
static std::vector<std::string> effects;
static bool waitForClock;
extern "C" int __wrap_have_ptp_timing_information(rtsp_conn_info *) { return 1; }
extern "C" void __wrap_reset_buffer(rtsp_conn_info *) { effects.push_back("reset"); }
extern "C" int __wrap_frame_to_local_time(uint32_t, uint64_t *time, rtsp_conn_info *) {
  effects.push_back("clock");
  *time = 1050000000;
  if (waitForClock) { waitForClock = false; return 1; }
  return 0;
}
extern "C" uint64_t __wrap_get_absolute_time_in_ns() { effects.push_back("now"); return 1000000000; }
extern "C" int __wrap_usleep(useconds_t duration) { effects.push_back("wait:" + std::to_string(duration)); return 0; }
extern "C" void __wrap__Z20prepareIncomingAudioR12SessionState9ssrc_type(SessionState &session, ssrc_t ssrc) {
  effects.push_back("prepare");
  session.inputAudio.recordPacketShape(*AudioFormat::fromSsrc(ssrc));
}
extern "C" uint32_t __wrap_player_put_packet(uint32_t, seq_t sequence, uint32_t timestamp,
    uint8_t *payload, size_t size, int mute, int32_t gap, rtsp_conn_info *) {
  effects.push_back("submit");
  submitted.push_back({sequence,timestamp,mute,gap,{payload,payload+size}});
  return 1024;
}
extern "C" void *rtp_buffered_audio_processor(void *);

TEST(BufferedPlaybackAdapter, RealEncryptedStreamPreservesClockOrderMuteAndOldBlockSkip) {
  submitted.clear(); effects.clear(); waitForClock = true;
  SessionState session;
  session.connection_number = 41;
  session.ap2_audio_buffer_size = 31;
  session.ap2_play_enabled = 1;
  session.session_key = static_cast<unsigned char *>(malloc(32));
  ASSERT_NE(session.session_key, nullptr);
  std::copy(buffered_block_fixture::key.begin(), buffered_block_fixture::key.end(), session.session_key);
  const int listener = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  ASSERT_GE(listener, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
  ASSERT_EQ(listen(listener, 1), 0);
  socklen_t size = sizeof(address);
  ASSERT_EQ(getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size), 0);
  session.buffered_audio_socket = listener;
  const int client = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(client, 0);
  ASSERT_EQ(connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
  for (const uint32_t timestamp : {1000u,2024u,100u}) {
    const auto block = buffered_block_fixture::encrypt(buffered_block_fixture::plaintext, AAC_48000_F24_2, timestamp);
    std::vector<uint8_t> bytes{0, static_cast<uint8_t>(block.size()+2)};
    bytes.insert(bytes.end(), block.begin(), block.end());
    ASSERT_EQ(send(client, bytes.data(), bytes.size(), MSG_NOSIGNAL), static_cast<ssize_t>(bytes.size()));
  }
  ASSERT_EQ(shutdown(client, SHUT_WR), 0);
  pthread_t processor;
  ASSERT_EQ(pthread_create(&processor, nullptr, rtp_buffered_audio_processor, &session), 0);
  ASSERT_EQ(pthread_join(processor, nullptr), 0);
  close(client);
  ASSERT_EQ(submitted.size(), 2u);
  EXPECT_EQ(submitted[0].sequence, 0xcdef);
  EXPECT_EQ(submitted[1].sequence, 0xcdf0);
  EXPECT_EQ(submitted[0].timestamp, 1000u);
  EXPECT_EQ(submitted[1].timestamp, 2024u);
  EXPECT_EQ(submitted[0].mute, 1);
  EXPECT_EQ(submitted[1].mute, 0);
  EXPECT_EQ(submitted[1].gap, 0);
  EXPECT_EQ(submitted[0].payload.size(), buffered_block_fixture::plaintext.size()+7);
  EXPECT_TRUE(std::equal(buffered_block_fixture::plaintext.begin(), buffered_block_fixture::plaintext.end(), submitted[0].payload.begin()+7));
  EXPECT_EQ(effects, (std::vector<std::string>{"reset","prepare","clock","wait:20000","clock","now","submit","clock","now","submit","clock","now"}));
  EXPECT_EQ(session.buffered_audio_socket, -1);
}
