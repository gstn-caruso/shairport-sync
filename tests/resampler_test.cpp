#include "session_state.hpp"
#include "resampler.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <array>

static unsigned initialized, released;
static bool observeFlush;
static int pendingBeforeReset;
extern "C" int __real_swr_init(SwrContext *);
extern "C" void __real_swr_free(SwrContext **);
extern "C" int __wrap_swr_init(SwrContext *context) {
  if (observeFlush)
    pendingBeforeReset = swr_get_out_samples(context, 0);
  ++initialized;
  return __real_swr_init(context);
}
extern "C" void __wrap_swr_free(SwrContext **context) {
  if (context && *context)
    ++released;
  __real_swr_free(context);
}

static OwnedAudioFrame samplesFor(AudioFormat format, AVSampleFormat sampleFormat) {
  OwnedAudioFrame frame(av_frame_alloc());
  frame->format = sampleFormat;
  frame->sample_rate = format.sampleRate();
  frame->nb_samples = 256;
  av_channel_layout_default(&frame->ch_layout, format.channels());
  assert(av_frame_get_buffer(frame.get(), 0) == 0);
  for (unsigned channel = 0; channel < format.channels(); ++channel)
    for (int sample = 0; sample < frame->nb_samples; ++sample)
      if (sampleFormat == AV_SAMPLE_FMT_S16P)
        reinterpret_cast<int16_t *>(frame->extended_data[channel])[sample] = 512 * (channel + 1);
      else if (sampleFormat == AV_SAMPLE_FMT_S32P)
        reinterpret_cast<int32_t *>(frame->extended_data[channel])[sample] = 0x1000000 * (channel + 1);
      else
        reinterpret_cast<float *>(frame->extended_data[channel])[sample] = 0.0625f * (channel + 1);
  return frame;
}

static void checkNativeFormats() {
  const std::array encodings{ALAC_44100_S16_2, ALAC_48000_S24_2, AAC_44100_F24_2,
                             AAC_48000_F24_2, AAC_48000_F24_5P1, AAC_48000_F24_7P1};
  for (auto encoding : encodings) {
    auto format = *AudioFormat::fromSsrc(encoding);
    const auto sampleFormat = format.isAac() ? AV_SAMPLE_FMT_FLTP :
        encoding == ALAC_44100_S16_2 ? AV_SAMPLE_FMT_S16P : AV_SAMPLE_FMT_S32P;
    auto frame = samplesFor(format, sampleFormat);
    for (unsigned rate : {format.sampleRate(), format.sampleRate() == 44100 ? 48000U : 44100U}) {
      Resampler resampler;
      OutputFormat output{rate, format.channels()};
      assert(resampler.configure(format, sampleFormat, output));
      auto converted = resampler.convert(*frame);
      assert(converted && converted->frames() > 0);
      assert(converted->bytes().size() == converted->frames() * output.channels *
                                         resampler.sampleBits() / 8);
      assert(resampler.effectiveSampleBits() == (format.isAac() ? 32 : format.sampleBits()));
      if (rate == format.sampleRate()) {
        assert(converted->frames() == 256 && converted->retainedFrames() == 0);
        if (resampler.sampleBits() == 16)
          assert(reinterpret_cast<const int16_t *>(converted->bytes().data())[0] == 512);
        else
          assert(reinterpret_cast<const int32_t *>(converted->bytes().data())[0] ==
                 (format.isAac() ? 0x8000000 : 0x1000000));
      } else {
        assert(converted->retainedFrames() > 0);
        const auto previousInitialization = initialized;
        const auto retained = resampler.retainedFrames();
        assert(resampler.configure(format, sampleFormat, output) == ResamplerChange::unchanged);
        assert(initialized == previousInitialization && resampler.retainedFrames() == retained);
        observeFlush = true;
        auto pending = resampler.flush();
        observeFlush = false;
        assert(pending && *pending == static_cast<size_t>(pendingBeforeReset));
        assert(*pending > 0 && resampler.retainedFrames() == 0);
      }
    }
  }
}

static void checkSilenceAndErrors() {
  Resampler resampler;
  assert(!resampler.silence(64));
  assert(resampler.flush() == 0);
  auto format = *AudioFormat::fromSsrc(ALAC_44100_S16_2);
  assert(resampler.configure(format, AV_SAMPLE_FMT_S16P, {44100, 1, 0, true}));
  auto mono = resampler.convert(*samplesFor(format, AV_SAMPLE_FMT_S16P));
  assert(mono && mono->frames() == 256 && mono->bytes().size() == 512);
  assert(resampler.configure(format, AV_SAMPLE_FMT_S16P, {44100, 2}) == ResamplerChange::changed);
  auto silence = resampler.silence(64);
  assert(silence && silence->frames() == 64 && silence->retainedFrames() == 0);
  for (auto byte : silence->bytes())
    assert(byte == 0);
  const auto previousRelease = released;
  assert(!resampler.configure(format, AV_SAMPLE_FMT_S16P, {0, 2}));
  assert(released == previousRelease && resampler.configuredFor(format));
  auto wrong = samplesFor(format, AV_SAMPLE_FMT_S32P);
  assert(!resampler.convert(*wrong));
  resampler.reset();
  assert(released == previousRelease + 1);
  resampler.reset();
  assert(released == previousRelease + 1);
}

int setup_software_resampler(rtsp_conn_info *, ssrc_t);
void clear_software_resampler(rtsp_conn_info *);
int64_t avframe_to_audio(rtsp_conn_info *, AVFrame *, uint8_t **, size_t *, size_t *);
static int32_t chooseStereo(unsigned, unsigned, unsigned) {
  return CHANNELS_TO_ENCODED_FORMAT(2) | RATE_TO_ENCODED_FORMAT(44100) |
         FORMAT_TO_ENCODED_FORMAT(SPS_FORMAT_S16_LE);
}
static int32_t rejectOutput(unsigned, unsigned, unsigned) { return 0; }

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
  backend.get_configuration = rejectOutput;
  const auto previousRate = session.input_rate, previousFrames = session.frames_per_packet;
  const auto previousConfiguration = config.current_output_configuration;
  setup_software_resampler(&session, ALAC_48000_S24_2);
  assert(session.input_rate == previousRate && session.frames_per_packet == previousFrames);
  assert(config.current_output_configuration == previousConfiguration);
  clear_software_resampler(&session);
  config.output = nullptr;
  Resampler resampler;
  auto format = *AudioFormat::fromSsrc(ALAC_44100_S16_2);
  OutputFormat output{44100, 2};
  assert(resampler.configure(format, AV_SAMPLE_FMT_S16P, output) == ResamplerChange::changed);
  auto converted = resampler.convert(*frame);
  assert(converted && converted->frames() == 64 && converted->bytes().size() == 256);
  assert(converted->retainedFrames() == 0);
  auto pcm = reinterpret_cast<const int16_t *>(converted->bytes().data());
  assert(pcm[0] == 5 && pcm[1] == 9);
  assert(resampler.configure(format, AV_SAMPLE_FMT_S16P, output) == ResamplerChange::unchanged);
  assert(resampler.configuredFor(format));
  assert(resampler.sampleBits() == 16 && resampler.effectiveSampleBits() == 16);
  resampler.reset();
  resampler.reset();
  assert(!resampler.configuredFor(format));
  checkNativeFormats();
  checkSilenceAndErrors();
}
