#include "buffered_block_fixture.hpp"
#include "session/session_state.hpp"
#include <gtest/gtest.h>
#include <span>

import receiver.protocol.ap2.buffered_block;

TEST(BufferedBlockLegacy, SodiumContractAndAdtsMatchDeterministicGoldenBytes) {
  ASSERT_GE(sodium_init(), 0);
  auto wire = buffered_block_fixture::encrypt();
  EXPECT_EQ(wire, std::vector<uint8_t>(buffered_block_fixture::golden.begin(), buffered_block_fixture::golden.end()));
  std::array<uint8_t, 12> nonce{};
  std::copy(wire.end() - 8, wire.end(), nonce.begin() + 4);
  std::array<uint8_t, 32> payload{};
  unsigned long long length = 0;
  ASSERT_EQ(crypto_aead_chacha20poly1305_ietf_decrypt(payload.data(), &length, nullptr,
      wire.data() + 12, wire.size() - 20, wire.data() + 4, 8, nonce.data(),
      buffered_block_fixture::key.data()), 0);
  EXPECT_EQ(length, buffered_block_fixture::plaintext.size());
  EXPECT_TRUE(std::equal(payload.begin(), payload.begin() + length,
      buffered_block_fixture::plaintext.begin(), buffered_block_fixture::plaintext.end()));
  std::array<uint8_t, 7> adts{};
  auto block = BufferedAudioBlock::parse(wire);
  ASSERT_TRUE(block);
  auto framed = block->prepare({BufferedBlockCodec::aac, 2}, buffered_block_fixture::key, 44100);
  ASSERT_TRUE(framed);
  std::copy_n(framed->begin(), 7, adts.begin());
  EXPECT_EQ(adts, (std::array<uint8_t,7>{0xff,0xf9,0x50,0x80,0x02,0xff,0xfc}));
}

struct HandoffCase { std::string name; bool aac; };
void PrintTo(const HandoffCase &scenario, std::ostream *output) { *output << scenario.name; }
class BufferedBlockHandoff : public testing::TestWithParam<HandoffCase> {};
TEST_P(BufferedBlockHandoff, OwnedPreparedPayloadDecodesSynchronouslyIntoRealPlayerQueue) {
  const bool aac = GetParam().aac;
  const auto format = *AudioFormat::fromSsrc(aac ? AAC_48000_F24_2 : ALAC_44100_S16_2);
  const auto releaseContext = [](AVCodecContext *context) { avcodec_free_context(&context); };
  const auto releaseFrame = [](AVFrame *frame) { av_frame_free(&frame); };
  const auto releasePacket = [](AVPacket *packet) { av_packet_free(&packet); };
  std::unique_ptr<AVCodecContext, decltype(releaseContext)> encoder(
      avcodec_alloc_context3(avcodec_find_encoder(aac ? AV_CODEC_ID_AAC : AV_CODEC_ID_ALAC)), releaseContext);
  ASSERT_TRUE(encoder);
  encoder->sample_fmt = aac ? AV_SAMPLE_FMT_FLTP : AV_SAMPLE_FMT_S16P;
  encoder->sample_rate = format.sampleRate();
  av_channel_layout_default(&encoder->ch_layout, 2);
  ASSERT_EQ(avcodec_open2(encoder.get(), encoder->codec, nullptr), 0);
  std::unique_ptr<AVFrame, decltype(releaseFrame)> frame(av_frame_alloc(), releaseFrame);
  ASSERT_TRUE(frame);
  frame->format = encoder->sample_fmt;
  frame->sample_rate = encoder->sample_rate;
  frame->nb_samples = format.framesPerPacket();
  ASSERT_EQ(av_channel_layout_copy(&frame->ch_layout, &encoder->ch_layout), 0);
  ASSERT_EQ(av_frame_get_buffer(frame.get(), 0), 0);
  for (unsigned channel = 0; channel < 2; ++channel)
    for (int sample = 0; sample < frame->nb_samples; ++sample) {
      if (aac)
        reinterpret_cast<float *>(frame->data[channel])[sample] = (sample % 17 - 8) * 0.03f;
      else
        reinterpret_cast<int16_t *>(frame->data[channel])[sample] = (sample % 11 - 5) * 3000;
    }
  ASSERT_EQ(avcodec_send_frame(encoder.get(), frame.get()), 0);
  std::unique_ptr<AVPacket, decltype(releasePacket)> packet(av_packet_alloc(), releasePacket);
  ASSERT_TRUE(packet);
  auto encoded = avcodec_receive_packet(encoder.get(), packet.get());
  if (encoded == AVERROR(EAGAIN)) {
    ASSERT_EQ(avcodec_send_frame(encoder.get(), nullptr), 0);
    encoded = avcodec_receive_packet(encoder.get(), packet.get());
  }
  ASSERT_EQ(encoded, 0);
  auto wire = buffered_block_fixture::encrypt(std::span(packet->data, packet->size), format.ssrc());
  auto block = BufferedAudioBlock::parse(wire);
  ASSERT_TRUE(block);
  auto prepared = block->prepare({aac ? BufferedBlockCodec::aac : BufferedBlockCodec::alac, 2},
      buffered_block_fixture::key, format.sampleRate());
  ASSERT_TRUE(prepared);
  SessionState session{};
  ASSERT_EQ(pthread_mutex_init(&session.flush_mutex, nullptr), 0);
  EXPECT_EQ(player_put_packet(format.ssrc(), 1, block->timestamp(), prepared->data(),
      prepared->size(), 0, 0, &session), format.framesPerPacket());
  prepared->clear();
  std::fill(wire.begin(), wire.end(), 0);
  auto queued = session.packetBuffer.front();
  ASSERT_TRUE(queued);
  EXPECT_TRUE(queued->packet.ready);
  EXPECT_EQ(queued->packet.frames, format.framesPerPacket());
  EXPECT_EQ(queued->sampleFormat, encoder->sample_fmt);
  EXPECT_EQ(pthread_mutex_destroy(&session.flush_mutex), 0);
}
INSTANTIATE_TEST_SUITE_P(Codecs, BufferedBlockHandoff, testing::Values(
    HandoffCase{"Alac",false}, HandoffCase{"Aac",true}
), [](const auto &scenario) { return scenario.param.name; });
