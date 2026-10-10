#include "session/session_state.hpp"
#include "audio/decoding/audio_decoder.hpp"
#include <gtest/gtest.h>
#include <cassert>
#include <string.h>
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
    assert(malloc_usable_size(context->extradata) >=
           context->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
    for (int index = 0; index < AV_INPUT_BUFFER_PADDING_SIZE; ++index)
      assert(context->extradata[context->extradata_size + index] == 0);
  }
  ++contextsOpened;
  return __real_avcodec_open2(context, codec, options);
}
extern "C" int __wrap_avcodec_send_packet(AVCodecContext *context, const AVPacket *packet) {
  assert(packet->buf != nullptr);
  assert(packet->buf->size >= packet->size + AV_INPUT_BUFFER_PADDING_SIZE);
  for (int index = 0; index < AV_INPUT_BUFFER_PADDING_SIZE; ++index)
    assert(packet->data[packet->size + index] == 0);
  return __real_avcodec_send_packet(context, packet);
}

static void checkPreparedFormatsThrough(AudioDecoder &decoder, ssrc_t lastEncoding) {
  const ssrc_t formats[] = {ALAC_44100_S16_2, ALAC_48000_S24_2, AAC_44100_F24_2,
                            AAC_48000_F24_2, AAC_48000_F24_5P1, AAC_48000_F24_7P1};
  for (size_t index = 0; index < sizeof(formats) / sizeof(formats[0]); index++) {
    auto format = *AudioFormat::fromSsrc(formats[index]);
    assert(decoder.prepare(format) == Preparation::changed);
    assert(decoder.currentFormat() == format);
    const auto opened = contextsOpened, released = contextsReleased;
    assert(decoder.prepare(format) == Preparation::unchanged);
    assert(contextsOpened == opened && contextsReleased == released);
    assert(decoder.currentFormat()->sampleRate() == (formats[index] == ALAC_44100_S16_2 ||
                                formats[index] == AAC_44100_F24_2 ? 44100U : 48000U));
    if (formats[index] == lastEncoding)
      break;
  }
}

static void checkUsedDecoderReset(AudioDecoder &decoder) {
  checkPreparedFormatsThrough(decoder, AAC_48000_F24_7P1);
  const auto beforeReset = contextsReleased;
  decoder.reset();
  assert(contextsReleased == beforeReset + 1);
  decoder.reset();
  assert(contextsReleased == beforeReset + 1);
  assert(!decoder.currentFormat());
}

static void checkAlacRoundtripAndFrameLifetime() {
  AudioDecoder decoder;
  checkUsedDecoderReset(decoder);
  AVCodecContext *encoder = avcodec_alloc_context3(avcodec_find_encoder(AV_CODEC_ID_ALAC));
  assert(encoder != NULL);
  encoder->sample_fmt = AV_SAMPLE_FMT_S16P;
  encoder->sample_rate = 44100;
  av_channel_layout_default(&encoder->ch_layout, 2);
  assert(avcodec_open2(encoder, encoder->codec, NULL) == 0);
  AVFrame *silence = av_frame_alloc();
  silence->format = encoder->sample_fmt;
  silence->sample_rate = encoder->sample_rate;
  silence->nb_samples = 352;
  assert(av_channel_layout_copy(&silence->ch_layout, &encoder->ch_layout) == 0);
  assert(av_frame_get_buffer(silence, 0) == 0);
  for (int channel = 0; channel < 2; channel++)
    memset(silence->data[channel], 0, silence->linesize[0]);
  assert(avcodec_send_frame(encoder, silence) == 0);
  AVPacket *packet = av_packet_alloc();
  assert(avcodec_receive_packet(encoder, packet) == 0);
  assert(decoder.prepare(*AudioFormat::fromSsrc(ALAC_44100_S16_2)) == Preparation::changed);
  auto decoded = decoder.decode({packet->data, static_cast<size_t>(packet->size)});
  assert(decoded);
  assert((*decoded)->nb_samples == 352);
  assert((*decoded)->sample_rate == 44100);
  assert((*decoded)->ch_layout.nb_channels == 2);
  assert((*decoded)->format == AV_SAMPLE_FMT_S16P);
  assert(decoder.decodedSampleFormat() == AV_SAMPLE_FMT_S16P);
  const auto beforeFrameRelease = framesReleased;
  decoder.reset();
  assert((*decoded)->nb_samples == 352);
  assert(framesReleased == beforeFrameRelease);
  decoded->reset();
  assert(framesReleased == beforeFrameRelease + 1);
  av_packet_free(&packet);
  av_frame_free(&silence);
  avcodec_free_context(&encoder);
  decoder.reset();
}

static void checkErrorsAndDestruction() {
  const auto before = contextsReleased;
  {
    AudioDecoder decoder;
    std::array<uint8_t, 8> shortPacket{};
    assert(decoder.decode(shortPacket).error().kind == DecoderFailure::Kind::packetTooShort);
    std::array<uint8_t, 16> invalidPacket{};
    assert(decoder.decode(invalidPacket).error().kind == DecoderFailure::Kind::notPrepared);
    assert(decoder.prepare(*AudioFormat::fromSsrc(ALAC_44100_S16_2)));
    auto invalid = decoder.decode(invalidPacket);
    assert(!invalid);
    assert(invalid.error().kind == DecoderFailure::Kind::sendFailed ||
           invalid.error().kind == DecoderFailure::Kind::receiveFailed);
    assert(decoder.currentFormat()->ssrc() == ALAC_44100_S16_2);
  }
  assert(contextsReleased == before + 1);
}

static void checkPlayerBoundary() {
  SessionState session{};
  prepare_decoding_chain(&session, ALAC_44100_S16_2);
  assert(session.inputAudio.sampleRate() == 44100 && session.inputAudio.framesPerPacket() == 352);
  assert(session.inputAudio.isDecodedFormatValid());
  prepare_decoding_chain(&session, static_cast<ssrc_t>(0xf00d));
  assert(session.decoder.currentFormat()->ssrc() == ALAC_44100_S16_2);
  assert(session.inputAudio.sampleRate() == 44100);
  std::array<uint8_t, 8> shortPacket{};
  assert(block_to_avframe(&session, shortPacket.data(), shortPacket.size()) == nullptr);
  std::array<uint8_t, 16> invalidPacket{};
  assert(block_to_avframe(&session, invalidPacket.data(), invalidPacket.size()) == nullptr);
  clear_decoding_chain(&session);
  clear_decoding_chain(&session);
  assert(!session.decoder.currentFormat());
}
TEST(AudioDecoder, Alac44100StereoPreparationReusesUnchangedContext) {
  AudioDecoder decoder;
  checkPreparedFormatsThrough(decoder, ALAC_44100_S16_2);
}

TEST(AudioDecoder, Alac48000StereoPreparationReplacesPriorFormatAndReusesContext) {
  AudioDecoder decoder;
  checkPreparedFormatsThrough(decoder, ALAC_48000_S24_2);
}

TEST(AudioDecoder, Aac44100StereoPreparationReplacesPriorFormatAndReusesContext) {
  AudioDecoder decoder;
  checkPreparedFormatsThrough(decoder, AAC_44100_F24_2);
}

TEST(AudioDecoder, Aac48000StereoPreparationReplacesPriorFormatAndReusesContext) {
  AudioDecoder decoder;
  checkPreparedFormatsThrough(decoder, AAC_48000_F24_2);
}

TEST(AudioDecoder, Aac48000Surround51PreparationReplacesPriorFormatAndReusesContext) {
  AudioDecoder decoder;
  checkPreparedFormatsThrough(decoder, AAC_48000_F24_5P1);
}

TEST(AudioDecoder, Aac48000Surround71PreparationReplacesPriorFormatAndReusesContext) {
  AudioDecoder decoder;
  checkPreparedFormatsThrough(decoder, AAC_48000_F24_7P1);
}

TEST(AudioDecoder, ResetAfterFormatChangesReleasesContextOnce) {
  AudioDecoder decoder;
  checkUsedDecoderReset(decoder);
}

TEST(AudioDecoder, AlacRoundtripFrameOutlivesDecoderReset) {
  checkAlacRoundtripAndFrameLifetime();
}

TEST(AudioDecoder, ShortUnpreparedAndInvalidPacketsPreserveFormatUntilDestruction) {
  checkErrorsAndDestruction();
}

TEST(AudioDecoder, PlayerBoundaryPreservesKnownFormatAndClearsIdempotently) {
  checkPlayerBoundary();
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
