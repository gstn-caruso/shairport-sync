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
static unsigned returnedFrames;
extern "C" int __wrap_have_ptp_timing_information(rtsp_conn_info *) { return 1; }
extern "C" void __wrap_reset_buffer(rtsp_conn_info *) { effects.push_back("reset"); }
extern "C" int __wrap_frame_to_local_time(uint32_t timestamp, uint64_t *time, rtsp_conn_info *) {
  effects.push_back("clock");
  *time = timestamp == 88 ? 999999999 : 1050000000;
  if (waitForClock) { waitForClock = false; return 1; }
  return 0;
}
extern "C" uint64_t __wrap_get_absolute_time_in_ns() { effects.push_back("now"); return 1000000000; }
extern "C" int __wrap_usleep(useconds_t duration) { effects.push_back("wait:" + std::to_string(duration)); return 0; }
extern "C" int __wrap_get_ptp_anchor_local_time_info(rtsp_conn_info *, uint32_t *, uint64_t *) {
  effects.push_back("anchor");
  return 1;
}
extern "C" int __real_crypto_aead_chacha20poly1305_ietf_decrypt(unsigned char *, unsigned long long *, unsigned char *, const unsigned char *, unsigned long long, const unsigned char *, unsigned long long, const unsigned char *, const unsigned char *);
extern "C" int __wrap_crypto_aead_chacha20poly1305_ietf_decrypt(unsigned char *plain, unsigned long long *size, unsigned char *secret, const unsigned char *cipher, unsigned long long cipherSize, const unsigned char *aad, unsigned long long aadSize, const unsigned char *nonce, const unsigned char *key) {
  effects.push_back("authenticate");
  return __real_crypto_aead_chacha20poly1305_ietf_decrypt(plain,size,secret,cipher,cipherSize,aad,aadSize,nonce,key);
}
extern "C" void __wrap__Z20prepareIncomingAudioR12SessionState9ssrc_type(SessionState &session, ssrc_t ssrc) {
  effects.push_back("prepare");
  session.inputAudio.recordPacketShape(*AudioFormat::fromSsrc(ssrc));
}
extern "C" uint32_t __wrap_player_put_packet(uint32_t, seq_t sequence, uint32_t timestamp,
    uint8_t *payload, size_t size, int mute, int32_t gap, rtsp_conn_info *) {
  effects.push_back("submit");
  submitted.push_back({sequence,timestamp,mute,gap,{payload,payload+size}});
  return returnedFrames;
}
extern "C" void *rtp_buffered_audio_processor(void *);

static void runStream(const std::vector<std::pair<uint32_t,bool>> &blocks) {
  SessionState session{};
  ASSERT_EQ(pthread_mutex_init(&session.flush_mutex, nullptr), 0);
  session.connection_number = 41;
  session.ap2_audio_buffer_size = 31;
  session.ap2_play_enabled = 1;
  session.session_key = const_cast<unsigned char *>(buffered_block_fixture::key.data());
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
  for (const auto &[timestamp, corrupted] : blocks) {
    auto block = buffered_block_fixture::encrypt(buffered_block_fixture::plaintext, AAC_48000_F24_2, timestamp);
    if (corrupted) block[12] ^= 1;
    std::vector<uint8_t> bytes{0, static_cast<uint8_t>(block.size()+2)};
    bytes.insert(bytes.end(), block.begin(), block.end());
    ASSERT_EQ(send(client, bytes.data(), bytes.size(), MSG_NOSIGNAL), static_cast<ssize_t>(bytes.size()));
  }
  ASSERT_EQ(shutdown(client, SHUT_WR), 0);
  pthread_t processor;
  ASSERT_EQ(pthread_create(&processor, nullptr, rtp_buffered_audio_processor, &session), 0);
  ASSERT_EQ(pthread_join(processor, nullptr), 0);
  close(client);
  ASSERT_EQ(pthread_mutex_destroy(&session.flush_mutex), 0);
  EXPECT_EQ(session.buffered_audio_socket, -1);
}
TEST(BufferedPlaybackAdapter, RealEncryptedStreamPreservesClockOrderMuteAndOldBlockSkip) {
  submitted.clear(); effects.clear(); waitForClock = true; returnedFrames = 1024;
  runStream({{1000,false},{2024,false},{100,false},{0x80000be8,false}});
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
  EXPECT_EQ(effects, (std::vector<std::string>{"reset","prepare","clock","wait:20000","clock","now","authenticate","submit","clock","now","authenticate","submit","clock","now","authenticate","clock","now","authenticate"}));
}
TEST(BufferedPlaybackAdapter, FailedAuthenticationDoesNotCommitAndLateBlocksNeverAuthenticate) {
  submitted.clear(); effects.clear(); waitForClock = true; returnedFrames = 1024;
  runStream({{77,true},{88,true},{1000,false},{2024,false}});
  ASSERT_EQ(submitted.size(), 2u);
  EXPECT_EQ(submitted[0].sequence, 0xcdef);
  EXPECT_EQ(submitted[1].sequence, 0xcdf0);
  EXPECT_EQ(submitted[0].timestamp, 1000u);
  EXPECT_EQ(submitted[0].mute, 1);
  EXPECT_EQ(submitted[1].mute, 0);
  EXPECT_EQ(submitted[1].gap, 0);
  EXPECT_EQ(effects, (std::vector<std::string>{"reset","prepare","clock","wait:20000","clock","now","authenticate","clock","now","anchor","clock","now","authenticate","submit","clock","now","authenticate","submit"}));
}
TEST(BufferedPlaybackAdapter, ZeroFramePlayerReturnStillCommitsSequenceAndFirstPacketState) {
  submitted.clear(); effects.clear(); waitForClock = false; returnedFrames = 0;
  runStream({{1000,false},{1000,false}});
  ASSERT_EQ(submitted.size(), 2u);
  EXPECT_EQ(submitted[0].sequence, 0xcdef);
  EXPECT_EQ(submitted[1].sequence, 0xcdf0);
  EXPECT_EQ(submitted[0].mute, 1);
  EXPECT_EQ(submitted[1].mute, 0);
  EXPECT_EQ(submitted[1].gap, 0);
}
