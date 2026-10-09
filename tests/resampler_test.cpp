#include "session_state.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>

int setup_software_resampler(rtsp_conn_info *, ssrc_t);
void clear_software_resampler(rtsp_conn_info *);
int64_t avframe_to_audio(rtsp_conn_info *, AVFrame *, uint8_t **, size_t *, size_t *);
static int32_t chooseStereo(unsigned, unsigned, unsigned) {
  return CHANNELS_TO_ENCODED_FORMAT(2) | RATE_TO_ENCODED_FORMAT(44100) |
         FORMAT_TO_ENCODED_FORMAT(SPS_FORMAT_S16_LE);
}

int main() {
  audio_output backend{};
  backend.get_configuration = chooseStereo;
  config.output = &backend;
  SessionState session{};
  prepare_decoding_chain(&session, ALAC_44100_S16_2);
  assert(setup_software_resampler(&session, ALAC_44100_S16_2) == 0);
  assert(session.input_bit_depth == 16 && session.input_effective_bit_depth == 16);
  OwnedAudioFrame frame(av_frame_alloc());
  frame->format = AV_SAMPLE_FMT_S16P;
  frame->sample_rate = 44100;
  frame->nb_samples = 64;
  av_channel_layout_default(&frame->ch_layout, 2);
  assert(av_frame_get_buffer(frame.get(), 0) == 0);
  auto *left = reinterpret_cast<int16_t *>(frame->data[0]);
  auto *right = reinterpret_cast<int16_t *>(frame->data[1]);
  for (int index = 0; index < 64; ++index) {
    left[index] = 5;
    right[index] = 9;
  }
  uint8_t *bytes;
  size_t length, count;
  auto retained = avframe_to_audio(&session, frame.get(), &bytes, &length, &count);
  assert(retained == 0 && count == 64 && length == 256);
  auto *result = reinterpret_cast<int16_t *>(bytes);
  assert(result[0] == 5 && result[1] == 9);
  free(bytes);
  config.output_channel_mapping_enable = 1;
  config.output_channel_map_size = 2;
  config.output_channel_map[0] = "FM";
  config.output_channel_map[1] = "--";
  assert(setup_software_resampler(&session, ALAC_44100_S16_2) == 0);
  avframe_to_audio(&session, frame.get(), &bytes, &length, &count);
  result = reinterpret_cast<int16_t *>(bytes);
  assert(result[0] == 6 && result[1] == 0);
  free(bytes);
  clear_software_resampler(&session);
  config.output = nullptr;
}
