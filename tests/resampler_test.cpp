#include "session_state.hpp"
#include "resampler.hpp"
#include "audio_player_adapter.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <array>
#include <algorithm>
#include <vector>

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

static void checkSilenceContinuity() {
  const auto format = *AudioFormat::fromSsrc(ALAC_44100_S16_2);
  auto frame = samplesFor(format, AV_SAMPLE_FMT_S16P);
  Resampler resampler;
  assert(resampler.configure(format, AV_SAMPLE_FMT_S16P, {48000, 2}));
  AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
  SwrContext *native = nullptr;
  assert(swr_alloc_set_opts2(&native, &stereo, AV_SAMPLE_FMT_S16, 48000,
                            &stereo, AV_SAMPLE_FMT_S16P, 44100, 0, nullptr) == 0);
  const auto freeContext = [](SwrContext *context) { swr_free(&context); };
  std::unique_ptr<SwrContext, decltype(freeContext)> reference(native, freeContext);
  assert(swr_init(reference.get()) == 0);
  const auto convertReference = [&](const uint8_t **input, int count) {
    const int capacity = swr_get_out_samples(reference.get(), count);
    assert(capacity >= 0);
    std::vector<uint8_t> bytes(static_cast<size_t>(capacity) * 4);
    uint8_t *output = bytes.data();
    const int generated = swr_convert(reference.get(), &output, capacity, input, count);
    assert(generated >= 0);
    bytes.resize(static_cast<size_t>(generated) * 4);
    return bytes;
  };
  const auto matches = [](const ConvertedAudio &actual, const std::vector<uint8_t> &expected) {
    assert(actual.frames() == expected.size() / 4);
    assert(std::equal(actual.bytes().begin(), actual.bytes().end(),
                      expected.begin(), expected.end()));
  };
  std::array<const uint8_t *, 2> input{frame->extended_data[0], frame->extended_data[1]};
  auto initial = resampler.convert(*frame);
  assert(initial && initial->retainedFrames() > 0);
  matches(*initial, convertReference(input.data(), frame->nb_samples));
  assert(swr_inject_silence(reference.get(), 64) == 0);
  std::array<const uint8_t *, 2> empty{};
  auto silence = resampler.silence(64);
  assert(silence);
  matches(*silence, convertReference(empty.data(), 0));
  auto following = resampler.convert(*frame);
  assert(following);
  matches(*following, convertReference(input.data(), frame->nb_samples));
}

int setup_software_resampler(rtsp_conn_info *, ssrc_t);
void clear_software_resampler(rtsp_conn_info *);
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
  auto initial = convertIncomingAudio(session, *frame);
  assert(initial.retainedFrames() == 0 && initial.frames() == 64 && initial.bytes().size() == 256);
  auto *result = reinterpret_cast<int16_t *>(initial.bytes().data());
  assert(result[0] == 5 && result[1] == 9);
  initial.reset();
  config.output_channel_mapping_enable = 1;
  config.output_channel_map_size = 2;
  config.output_channel_map[0] = "FM";
  config.output_channel_map[1] = "--";
  assert(setup_software_resampler(&session, ALAC_44100_S16_2) == 0);
  auto mapped = convertIncomingAudio(session, *frame);
  result = reinterpret_cast<int16_t *>(mapped.bytes().data());
  assert(result[0] == 6 && result[1] == 0);
  mapped.reset();
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
  checkSilenceContinuity();
}
