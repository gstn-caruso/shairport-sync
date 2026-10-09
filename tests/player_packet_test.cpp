#include "session_state.hpp"
#include <array>
#include <cassert>

int main() {
  SessionState session{};
  assert(pthread_mutex_init(&session.ab_mutex, nullptr) == 0);
  assert(pthread_mutex_init(&session.flush_mutex, nullptr) == 0);
  assert(pthread_cond_init(&session.flowcontrol, nullptr) == 0);
  std::array<uint8_t, 16> invalid{};
  const std::array formats{ALAC_44100_S16_2, AAC_48000_F24_2};
  for (uint16_t sequence = 0; sequence < formats.size(); ++sequence) {
    const auto encoding = formats[sequence];
    assert(player_put_packet(encoding, sequence, 1000 + sequence, invalid.data(),
                             sequence == 0 ? 8 : invalid.size(), 1, 0, &session) == 0);
    const auto &packet = session.audio_buffer[sequence];
    assert(packet.avframe == nullptr && packet.ready);
    assert(packet.length == AudioFormat::fromSsrc(encoding)->framesPerPacket());
  }
  assert(pthread_cond_destroy(&session.flowcontrol) == 0);
  assert(pthread_mutex_destroy(&session.ab_mutex) == 0);
  assert(pthread_mutex_destroy(&session.flush_mutex) == 0);
  abuf_t pcm{};
  pcm.timestamp = 100;
  pcm.length = 4;
  pcm.data = *ConvertedAudio::allocate(16, 4, 0);
  auto *samples = reinterpret_cast<int16_t *>(pcm.data.bytes().data());
  for (int index = 0; index < 8; ++index)
    samples[index] = index + 1;
  pcm.trimBefore(102, 4);
  assert(pcm.timestamp == 102 && pcm.length == 2);
  assert(samples[0] == 5 && samples[3] == 8);
  abuf_t decoded{};
  OwnedAudioFrame frame(av_frame_alloc());
  frame->format = AV_SAMPLE_FMT_S16P;
  frame->sample_rate = 44100;
  frame->nb_samples = 16;
  av_channel_layout_default(&frame->ch_layout, 2);
  assert(av_frame_get_buffer(frame.get(), 0) == 0);
  for (int index = 0; index < 16; ++index) {
    reinterpret_cast<int16_t *>(frame->data[0])[index] = index;
    reinterpret_cast<int16_t *>(frame->data[1])[index] = 100 + index;
  }
  OwnedAudioFrame shared(av_frame_clone(frame.get()));
  decoded.avframe = frame.release();
  decoded.timestamp = 200;
  decoded.length = 16;
  decoded.trimBefore(205, 4);
  assert(decoded.timestamp == 205 && decoded.length == 11);
  assert(decoded.avframe->nb_samples == 16);
  decoded.prepareForConversion();
  assert(decoded.avframe->nb_samples == 11);
  assert(av_frame_is_writable(decoded.avframe));
  assert(reinterpret_cast<int16_t *>(decoded.avframe->data[0])[0] == 5);
  assert(reinterpret_cast<int16_t *>(decoded.avframe->data[1])[0] == 105);
  assert(shared->nb_samples == 16 && reinterpret_cast<int16_t *>(shared->data[0])[0] == 0);
  decoded.prepareForConversion();
  assert(decoded.avframe->nb_samples == 11);
  av_frame_free(&decoded.avframe);
}
