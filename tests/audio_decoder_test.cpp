#include "session_state.hpp"
#include "common.h"
#include "rtsp.h"
#include "rtsp_message.hpp"
#include "utilities/rtsp_message_utilities.h"
#include <assert.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

extern "C" int __real_avcodec_open2(AVCodecContext *, const AVCodec *, AVDictionary **);
extern "C" int __real_avcodec_send_packet(AVCodecContext *, const AVPacket *);
extern "C" int __wrap_avcodec_open2(AVCodecContext *context, const AVCodec *codec,
                                   AVDictionary **options) {
  if (context->extradata_size > 0) {
    assert(malloc_usable_size(context->extradata) >=
           context->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
    for (int index = 0; index < AV_INPUT_BUFFER_PADDING_SIZE; ++index)
      assert(context->extradata[context->extradata_size + index] == 0);
  }
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
  rtsp_conn_info conn{};
  const ssrc_t formats[] = {ALAC_44100_S16_2, ALAC_48000_S24_2, AAC_44100_F24_2,
                            AAC_48000_F24_2, AAC_48000_F24_5P1, AAC_48000_F24_7P1};
  for (size_t index = 0; index < sizeof(formats) / sizeof(formats[0]); index++) {
    prepare_decoding_chain(&conn, formats[index]);
    assert(conn.codec_context != NULL);
    assert(conn.ffmpeg_decoding_chain_initialised);
    assert(conn.input_rate == (formats[index] == ALAC_44100_S16_2 ||
                                formats[index] == AAC_44100_F24_2 ? 44100U : 48000U));
  }
  clear_decoding_chain(&conn);

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
  prepare_decoding_chain(&conn, ALAC_44100_S16_2);
  AVFrame *decoded = block_to_avframe(&conn, packet->data, packet->size);
  assert(decoded != NULL);
  assert(decoded->nb_samples == 352);
  assert(decoded->sample_rate == 44100);
  assert(decoded->ch_layout.nb_channels == 2);
  assert(decoded->format == AV_SAMPLE_FMT_S16P);
  av_frame_free(&decoded);
  av_packet_free(&packet);
  av_frame_free(&silence);
  avcodec_free_context(&encoder);
  clear_decoding_chain(&conn);
}

int main() { check_audio_formats(); }
