#include "buffered_block_fixture.hpp"
#include "session/session_state.hpp"
#include <gtest/gtest.h>
#include <cstring>
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
        reinterpret_cast<float *>(frame->data[channel])[sample] = channel == 0
            ? (sample % 17 - 8) * 0.03f : (sample % 23 - 11) * 0.02f + 0.05f;
      else
        reinterpret_cast<int16_t *>(frame->data[channel])[sample] = channel == 0
            ? (sample % 11 - 5) * 3000 : -12000 + sample * 60;
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
  std::vector<uint8_t> aacReference;
  if (aac) {
    AudioDecoder referenceDecoder;
    ASSERT_TRUE(referenceDecoder.prepare(format));
    auto referenceFrame = referenceDecoder.decode(*prepared);
    ASSERT_TRUE(referenceFrame);
    Resampler referenceResampler;
    ASSERT_TRUE(referenceResampler.configure(format, AV_SAMPLE_FMT_FLTP, {format.sampleRate(), 2}));
    auto converted = referenceResampler.convert(**referenceFrame);
    ASSERT_TRUE(converted);
    aacReference.assign(converted->bytes().begin(), converted->bytes().end());
  }
  SessionState session{};
  ASSERT_EQ(pthread_mutex_init(&session.flush_mutex, nullptr), 0);
  EXPECT_EQ(player_put_packet(format.ssrc(), 1, block->timestamp(), prepared->data(),
      prepared->size(), 0, 0, &session), format.framesPerPacket());
  std::fill(prepared->begin(), prepared->end(), 0xa5);
  std::fill(wire.begin(), wire.end(), 0);
  std::vector<uint8_t>().swap(*prepared);
  std::vector<uint8_t>().swap(wire);
  packet.reset();
  EXPECT_EQ(prepared->capacity(), 0u);
  EXPECT_EQ(wire.capacity(), 0u);
  auto queued = session.packetBuffer.front();
  ASSERT_TRUE(queued);
  EXPECT_TRUE(queued->packet.ready);
  EXPECT_EQ(queued->packet.frames, format.framesPerPacket());
  EXPECT_EQ(queued->sampleFormat, encoder->sample_fmt);
  auto front = session.packetBuffer.takeFrontIf(queued->revision);
  ASSERT_TRUE(front);
  auto &audio = std::get<QueuedAudioPacket>(*front);
  Resampler queuedResampler;
  ASSERT_TRUE(queuedResampler.configure(format, queued->sampleFormat, {format.sampleRate(), 2}));
  ASSERT_TRUE(audio.convertWith(queuedResampler));
  const auto bytes = audio.audioBytes();
  const std::size_t sampleBytes = aac ? sizeof(int32_t) : sizeof(int16_t);
  ASSERT_EQ(bytes.size(), format.framesPerPacket() * 2 * sampleBytes);
  if (aac)
    ASSERT_EQ(aacReference.size(), bytes.size());
  bool nonSilent = false, differentChannels = false;
  for (unsigned sample = 0; sample < format.framesPerPacket(); ++sample) {
    SCOPED_TRACE(sample);
    if (aac) {
      int32_t left, right, expectedLeft, expectedRight;
      std::memcpy(&left, bytes.data() + 2 * sample * sampleBytes, sampleBytes);
      std::memcpy(&right, bytes.data() + (2 * sample + 1) * sampleBytes, sampleBytes);
      std::memcpy(&expectedLeft, aacReference.data() + 2 * sample * sampleBytes, sampleBytes);
      std::memcpy(&expectedRight, aacReference.data() + (2 * sample + 1) * sampleBytes, sampleBytes);
      EXPECT_EQ(left, expectedLeft);
      EXPECT_EQ(right, expectedRight);
      nonSilent |= expectedLeft != 0 || expectedRight != 0;
      differentChannels |= expectedLeft != expectedRight;
    } else {
      int16_t left, right;
      std::memcpy(&left, bytes.data() + 2 * sample * sampleBytes, sampleBytes);
      std::memcpy(&right, bytes.data() + (2 * sample + 1) * sampleBytes, sampleBytes);
      EXPECT_EQ(left, (int(sample) % 11 - 5) * 3000);
      EXPECT_EQ(right, -12000 + int(sample) * 60);
      nonSilent |= left != 0 || right != 0;
      differentChannels |= left != right;
    }
  }
  EXPECT_TRUE(nonSilent);
  EXPECT_TRUE(differentChannels);
  EXPECT_EQ(pthread_mutex_destroy(&session.flush_mutex), 0);
}
INSTANTIATE_TEST_SUITE_P(Codecs, BufferedBlockHandoff, testing::Values(
    HandoffCase{"Alac",false}, HandoffCase{"Aac",true}
), [](const auto &scenario) { return scenario.param.name; });
