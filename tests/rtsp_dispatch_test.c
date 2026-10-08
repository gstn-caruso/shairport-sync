#undef main
#include "common.h"
#include "rtsp.h"
#include "utilities/rtsp_message_utilities.h"
#include <assert.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>
#include <string.h>

AVFrame *block_to_avframe(rtsp_conn_info *conn, uint8_t *data, size_t length);

static void check_audio_formats(void) {
  rtsp_conn_info conn = {0};
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
  av_frame_free(&decoded);
  av_packet_free(&packet);
  av_frame_free(&silence);
  avcodec_free_context(&encoder);
  clear_decoding_chain(&conn);
}

int main(void) {
  check_audio_formats();
  rtsp_conn_info conn = {0};
  conn.thread = pthread_self();
  rtsp_message req = {0};
  rtsp_message *resp = msg_init();
  strcpy(req.method, "OPTIONS");
  rtsp_dispatch_request(&conn, &req, resp);
  assert(resp->respcode == 200);
  const char *methods = msg_get_header(resp, "Public");
  assert(methods != NULL);
  assert(strstr(methods, "ANNOUNCE") == NULL);
  assert(strstr(methods, "FLUSHBUFFERED") != NULL);
  assert(strstr(methods, "GET_PARAMETER") != NULL);
  msg_free(&resp);
  const char *unsupported[] = {"ANNOUNCE", "PAUSE", "UNSUPPORTED"};
  for (size_t index = 0; index < sizeof(unsupported) / sizeof(unsupported[0]); index++) {
    resp = msg_init();
    strcpy(req.method, unsupported[index]);
    rtsp_dispatch_request(&conn, &req, resp);
    assert(resp->respcode == 501);
    msg_free(&resp);
  }
  plist_t setup = plist_new_dict();
  plist_dict_set_item(setup, "timingProtocol", plist_new_string("NTP"));
  plist_to_bin(setup, &req.content, &req.contentlength);
  plist_free(setup);
  strcpy(req.method, "SETUP");
  resp = msg_init();
  rtsp_dispatch_request(&conn, &req, resp);
  assert(resp->respcode == 400);
  msg_free(&resp);
  free(req.content);
  puts("Only AirPlay 2 methods advertised.");
  return 0;
}
