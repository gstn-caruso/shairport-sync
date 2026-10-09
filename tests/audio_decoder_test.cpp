#include "session_state.hpp"
#include "audio_decoder.hpp"
#include <cassert>
#include <string.h>
#include <malloc.h>
#include <array>

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

static void check_audio_formats(void) {
  AudioDecoder decoder;
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
  }
  const auto beforeReset = contextsReleased;
  decoder.reset();
  assert(contextsReleased == beforeReset + 1);
  decoder.reset();
  assert(contextsReleased == beforeReset + 1);
  assert(!decoder.currentFormat());

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
  assert(session.input_rate == 44100 && session.frames_per_packet == 352);
  assert(session.input_format_is_valid);
  prepare_decoding_chain(&session, static_cast<ssrc_t>(0xf00d));
  assert(session.decoder.currentFormat()->ssrc() == ALAC_44100_S16_2);
  assert(session.input_rate == 44100);
  std::array<uint8_t, 8> shortPacket{};
  assert(block_to_avframe(&session, shortPacket.data(), shortPacket.size()) == nullptr);
  std::array<uint8_t, 16> invalidPacket{};
  assert(block_to_avframe(&session, invalidPacket.data(), invalidPacket.size()) == nullptr);
  clear_decoding_chain(&session);
  clear_decoding_chain(&session);
  assert(!session.decoder.currentFormat());
}
int main() {
  check_audio_formats();
  checkErrorsAndDestruction();
  checkPlayerBoundary();
}
