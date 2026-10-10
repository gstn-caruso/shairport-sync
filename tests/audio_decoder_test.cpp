#include "session/session_state.hpp"
#include "audio/decoding/audio_decoder.hpp"
#include <gtest/gtest.h>
#include <malloc.h>
#include <array>
#include <memory>
#include <cstring>

static unsigned contextsOpened, contextsReleased, framesReleased;

extern "C" int __real_avcodec_open2(AVCodecContext *, const AVCodec *, AVDictionary **);
extern "C" int __real_avcodec_send_packet(AVCodecContext *, const AVPacket *);
extern "C" void __real_avcodec_free_context(AVCodecContext **);
extern "C" void __real_av_frame_free(AVFrame **);
extern "C" void __wrap_avcodec_free_context(AVCodecContext **context) {
  if (context && *context)
    ++contextsReleased;
  __real_avcodec_free_context(context);
}
extern "C" void __wrap_av_frame_free(AVFrame **frame) {
  if (frame && *frame)
    ++framesReleased;
  __real_av_frame_free(frame);
}
extern "C" int __wrap_avcodec_open2(AVCodecContext *context, const AVCodec *codec,
                                   AVDictionary **options) {
  if (context->extradata_size > 0) {
    const auto required = context->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE;
    EXPECT_GE(malloc_usable_size(context->extradata), required);
    if (malloc_usable_size(context->extradata) < static_cast<size_t>(required))
      return AVERROR(EINVAL);
    for (int index = 0; index < AV_INPUT_BUFFER_PADDING_SIZE; ++index)
      EXPECT_EQ(context->extradata[context->extradata_size + index], 0) << "Padding byte " << index;
  }
  ++contextsOpened;
  return __real_avcodec_open2(context, codec, options);
}
extern "C" int __wrap_avcodec_send_packet(AVCodecContext *context, const AVPacket *packet) {
  EXPECT_NE(packet->buf, nullptr);
  if (!packet->buf)
    return AVERROR(EINVAL);
  EXPECT_GE(packet->buf->size, packet->size + AV_INPUT_BUFFER_PADDING_SIZE);
  if (packet->buf->size < packet->size + AV_INPUT_BUFFER_PADDING_SIZE)
    return AVERROR(EINVAL);
  for (int index = 0; index < AV_INPUT_BUFFER_PADDING_SIZE; ++index)
    EXPECT_EQ(packet->data[packet->size + index], 0) << "Padding byte " << index;
  return __real_avcodec_send_packet(context, packet);
}

struct DecoderFormatCase {
  const char *name;
  std::optional<ssrc_t> previous;
  ssrc_t encoding;
  unsigned sampleRate;
};

void PrintTo(const DecoderFormatCase &format, std::ostream *output) { *output << format.name; }

class DecoderFormat : public testing::TestWithParam<DecoderFormatCase> {};

TEST_P(DecoderFormat, PreparationReplacesPriorFormatAndReusesUnchangedContext) {
  AudioDecoder decoder;
  if (GetParam().previous)
    ASSERT_TRUE(decoder.prepare(*AudioFormat::fromSsrc(*GetParam().previous)));
  auto format = *AudioFormat::fromSsrc(GetParam().encoding);
  const auto beforeOpen = contextsOpened, beforeRelease = contextsReleased;

  EXPECT_EQ(decoder.prepare(format), Preparation::changed);

  ASSERT_EQ(decoder.currentFormat(), format);
  EXPECT_EQ(contextsOpened, beforeOpen + 1);
  EXPECT_EQ(contextsReleased, beforeRelease + (GetParam().previous ? 1 : 0));
  EXPECT_EQ(decoder.currentFormat()->sampleRate(), GetParam().sampleRate);
  const auto opened = contextsOpened, released = contextsReleased;
  EXPECT_EQ(decoder.prepare(format), Preparation::unchanged);
  EXPECT_EQ(contextsOpened, opened);
  EXPECT_EQ(contextsReleased, released);
}

INSTANTIATE_TEST_SUITE_P(Formats, DecoderFormat, testing::Values(
  DecoderFormatCase{"Alac44100Stereo", std::nullopt, ALAC_44100_S16_2, 44100},
  DecoderFormatCase{"Alac48000Stereo", ALAC_44100_S16_2, ALAC_48000_S24_2, 48000},
  DecoderFormatCase{"Aac44100Stereo", ALAC_48000_S24_2, AAC_44100_F24_2, 44100},
  DecoderFormatCase{"Aac48000Stereo", AAC_44100_F24_2, AAC_48000_F24_2, 48000},
  DecoderFormatCase{"Aac48000Surround51", AAC_48000_F24_2, AAC_48000_F24_5P1, 48000},
  DecoderFormatCase{"Aac48000Surround71", AAC_48000_F24_5P1, AAC_48000_F24_7P1, 48000}
), [](const auto &info) { return info.param.name; });

TEST(AudioDecoder, ResetAfterFormatChangesReleasesContextOnce) {
  AudioDecoder decoder;
  ASSERT_TRUE(decoder.prepare(*AudioFormat::fromSsrc(ALAC_44100_S16_2)));
  ASSERT_TRUE(decoder.prepare(*AudioFormat::fromSsrc(AAC_48000_F24_7P1)));
  const auto beforeReset = contextsReleased;
  decoder.reset();
  EXPECT_EQ(contextsReleased, beforeReset + 1);
  decoder.reset();
  EXPECT_EQ(contextsReleased, beforeReset + 1);
  EXPECT_FALSE(decoder.currentFormat());
}

TEST(AudioDecoder, AlacRoundtripFrameOutlivesDecoderReset) {
  AudioDecoder decoder;
  AVCodecContext *encoder = avcodec_alloc_context3(avcodec_find_encoder(AV_CODEC_ID_ALAC));
  const auto releaseEncoder = [](AVCodecContext *context) { avcodec_free_context(&context); };
  const std::unique_ptr<AVCodecContext, decltype(releaseEncoder)> encoderOwner(encoder, releaseEncoder);
  ASSERT_NE(encoder, nullptr);
  encoder->sample_fmt = AV_SAMPLE_FMT_S16P;
  encoder->sample_rate = 44100;
  av_channel_layout_default(&encoder->ch_layout, 2);
  ASSERT_EQ(avcodec_open2(encoder, encoder->codec, nullptr), 0);
  AVFrame *silence = av_frame_alloc();
  const auto releaseInput = [](AVFrame *frame) { av_frame_free(&frame); };
  const std::unique_ptr<AVFrame, decltype(releaseInput)> inputOwner(silence, releaseInput);
  ASSERT_NE(silence, nullptr);
  silence->format = encoder->sample_fmt;
  silence->sample_rate = encoder->sample_rate;
  silence->nb_samples = 352;
  ASSERT_EQ(av_channel_layout_copy(&silence->ch_layout, &encoder->ch_layout), 0);
  ASSERT_EQ(av_frame_get_buffer(silence, 0), 0);
  for (int channel = 0; channel < 2; channel++)
    memset(silence->data[channel], 0, silence->linesize[0]);
  ASSERT_EQ(avcodec_send_frame(encoder, silence), 0);
  AVPacket *packet = av_packet_alloc();
  const auto releasePacket = [](AVPacket *value) { av_packet_free(&value); };
  const std::unique_ptr<AVPacket, decltype(releasePacket)> packetOwner(packet, releasePacket);
  ASSERT_NE(packet, nullptr);
  ASSERT_EQ(avcodec_receive_packet(encoder, packet), 0);
  ASSERT_EQ(decoder.prepare(*AudioFormat::fromSsrc(ALAC_44100_S16_2)), Preparation::changed);
  auto decoded = decoder.decode({packet->data, static_cast<size_t>(packet->size)});
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ((*decoded)->nb_samples, 352);
  EXPECT_EQ((*decoded)->sample_rate, 44100);
  EXPECT_EQ((*decoded)->ch_layout.nb_channels, 2);
  EXPECT_EQ((*decoded)->format, AV_SAMPLE_FMT_S16P);
  EXPECT_EQ(decoder.decodedSampleFormat(), AV_SAMPLE_FMT_S16P);
  const auto beforeFrameRelease = framesReleased;
  decoder.reset();
  EXPECT_EQ((*decoded)->nb_samples, 352);
  EXPECT_EQ(framesReleased, beforeFrameRelease);
  decoded->reset();
  EXPECT_EQ(framesReleased, beforeFrameRelease + 1);
  decoder.reset();
}

TEST(AudioDecoder, EightBytePacketIsTooShort) {
  AudioDecoder decoder;
  std::array<uint8_t, 8> shortPacket{};

  auto result = decoder.decode(shortPacket);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, DecoderFailure::Kind::packetTooShort);
}

TEST(AudioDecoder, DecodingBeforePreparationReportsMissingFormat) {
  AudioDecoder decoder;
  std::array<uint8_t, 16> packet{};

  auto result = decoder.decode(packet);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, DecoderFailure::Kind::notPrepared);
}

TEST(AudioDecoder, InvalidEncodedPacketPreservesFormatUntilDestruction) {
  const auto before = contextsReleased;
  {
    AudioDecoder decoder;
    std::array<uint8_t, 16> invalidPacket{};
    ASSERT_TRUE(decoder.prepare(*AudioFormat::fromSsrc(ALAC_44100_S16_2)));
    auto invalid = decoder.decode(invalidPacket);
    ASSERT_FALSE(invalid.has_value());
    EXPECT_TRUE(invalid.error().kind == DecoderFailure::Kind::sendFailed ||
                invalid.error().kind == DecoderFailure::Kind::receiveFailed)
        << "Failure kind " << static_cast<int>(invalid.error().kind);
    ASSERT_TRUE(decoder.currentFormat().has_value());
    EXPECT_EQ(decoder.currentFormat()->ssrc(), ALAC_44100_S16_2);
  }
  EXPECT_EQ(contextsReleased, before + 1);
}

TEST(AudioDecoder, PlayerBoundaryPreservesKnownFormatAndClearsIdempotently) {
  SessionState session{};
  prepare_decoding_chain(&session, ALAC_44100_S16_2);
  EXPECT_EQ(session.inputAudio.sampleRate(), 44100);
  EXPECT_EQ(session.inputAudio.framesPerPacket(), 352);
  EXPECT_TRUE(session.inputAudio.isDecodedFormatValid());
  prepare_decoding_chain(&session, static_cast<ssrc_t>(0xf00d));
  ASSERT_TRUE(session.decoder.currentFormat().has_value());
  EXPECT_EQ(session.decoder.currentFormat()->ssrc(), ALAC_44100_S16_2);
  EXPECT_EQ(session.inputAudio.sampleRate(), 44100);
  std::array<uint8_t, 8> shortPacket{};
  EXPECT_EQ(block_to_avframe(&session, shortPacket.data(), shortPacket.size()), nullptr);
  std::array<uint8_t, 16> invalidPacket{};
  EXPECT_EQ(block_to_avframe(&session, invalidPacket.data(), invalidPacket.size()), nullptr);
  clear_decoding_chain(&session);
  clear_decoding_chain(&session);
  EXPECT_FALSE(session.decoder.currentFormat());
}

TEST(AudioDecoder, NonSilentAlacRoundtripPreservesDistinctLeftAndRightSamples) {
  const auto releaseContext = [](AVCodecContext *context) { avcodec_free_context(&context); };
  const auto releaseFrame = [](AVFrame *frame) { av_frame_free(&frame); };
  const auto releasePacket = [](AVPacket *packet) { av_packet_free(&packet); };
  std::unique_ptr<AVCodecContext, decltype(releaseContext)> encoder(
      avcodec_alloc_context3(avcodec_find_encoder(AV_CODEC_ID_ALAC)), releaseContext);
  ASSERT_NE(encoder, nullptr);
  encoder->sample_fmt = AV_SAMPLE_FMT_S16P;
  encoder->sample_rate = 44100;
  av_channel_layout_default(&encoder->ch_layout, 2);
  ASSERT_EQ(avcodec_open2(encoder.get(), encoder->codec, nullptr), 0);

  std::unique_ptr<AVFrame, decltype(releaseFrame)> input(av_frame_alloc(), releaseFrame);
  ASSERT_NE(input, nullptr);
  input->format = encoder->sample_fmt;
  input->sample_rate = encoder->sample_rate;
  input->nb_samples = 352;
  ASSERT_EQ(av_channel_layout_copy(&input->ch_layout, &encoder->ch_layout), 0);
  ASSERT_EQ(av_frame_get_buffer(input.get(), 0), 0);
  std::array<std::array<int16_t, 352>, 2> expected{};
  for (int frame = 0; frame < 352; ++frame) {
    expected[0][frame] = static_cast<int16_t>((frame % 11 - 5) * 3000);
    expected[1][frame] = static_cast<int16_t>(-12000 + frame * 60);
  }
  for (size_t channel = 0; channel < expected.size(); ++channel)
    std::memcpy(input->data[channel], expected[channel].data(), sizeof(expected[channel]));
  ASSERT_EQ(avcodec_send_frame(encoder.get(), input.get()), 0);
  std::unique_ptr<AVPacket, decltype(releasePacket)> packet(av_packet_alloc(), releasePacket);
  ASSERT_NE(packet, nullptr);
  ASSERT_EQ(avcodec_receive_packet(encoder.get(), packet.get()), 0);
  AudioDecoder decoder;
  ASSERT_TRUE(decoder.prepare(*AudioFormat::fromSsrc(ALAC_44100_S16_2)));

  auto decoded = decoder.decode({packet->data, static_cast<size_t>(packet->size)});

  ASSERT_TRUE(decoded.has_value());
  ASSERT_EQ((*decoded)->nb_samples, 352);
  ASSERT_EQ((*decoded)->format, AV_SAMPLE_FMT_S16P);
  ASSERT_EQ((*decoded)->ch_layout.nb_channels, 2);
  for (size_t channel = 0; channel < expected.size(); ++channel) {
    SCOPED_TRACE(testing::Message() << "Channel " << channel);
    std::array<int16_t, 352> actual;
    std::memcpy(actual.data(), (*decoded)->data[channel], sizeof(actual));
    EXPECT_EQ(actual, expected[channel]);
  }
}
