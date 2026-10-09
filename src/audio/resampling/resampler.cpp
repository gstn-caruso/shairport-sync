#include "audio/resampling/resampler.hpp"
#include <array>
#include <climits>
#include <new>

namespace {
uint64_t defaultLayout(unsigned channels) {
#if LIBAVUTIL_VERSION_MAJOR >= 57
  AVChannelLayout layout{};
  av_channel_layout_default(&layout, channels);
  const auto mask = layout.u.mask;
  av_channel_layout_uninit(&layout);
  return mask;
#else
  return av_get_default_channel_layout(channels);
#endif
}
int setLayout(SwrContext *context, const char *name, uint64_t mask) {
#if LIBAVUTIL_VERSION_MAJOR >= 57
  AVChannelLayout layout{};
  const int decoded = av_channel_layout_from_mask(&layout, mask);
  const int result = decoded < 0 ? decoded : av_opt_set_chlayout(context, name, &layout, 0);
  av_channel_layout_uninit(&layout);
  return result;
#else
  return av_opt_set_int(context, name, mask, 0);
#endif
}
}

uint64_t Resampler::inputLayout(const Configuration &configuration) {
  return configuration.output.inputLayout ? configuration.output.inputLayout
                                           : defaultLayout(configuration.input.channels());
}
uint64_t Resampler::convertedLayout(const Configuration &configuration) {
  if (!configuration.output.mixdown)
    return inputLayout(configuration);
  if (configuration.output.mixdownLayout)
    return configuration.output.mixdownLayout;
  return configuration.output.channels < configuration.input.channels()
             ? defaultLayout(configuration.output.channels) : inputLayout(configuration);
}
AVSampleFormat Resampler::intermediateFormat(AudioFormat format) {
  return format.ssrc() == ALAC_44100_S16_2 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_S32;
}

std::vector<std::string> Resampler::channelNames(uint64_t mask) {
  std::vector<std::string> names;
#if LIBAVUTIL_VERSION_MAJOR >= 57
  AVChannelLayout layout{};
  av_channel_layout_from_mask(&layout, mask);
  std::array<std::array<char, 32>, 64> buffers{};
  for (int index = 0; index < layout.nb_channels && index < 64; ++index)
    av_channel_name(buffers[index].data(), buffers[index].size(),
                    av_channel_layout_channel_from_index(&layout, index));
  const int count = layout.nb_channels;
  av_channel_layout_uninit(&layout);
  for (int index = 0; index < count && index < 64; ++index)
    names.emplace_back(buffers[index].data());
#else
  for (unsigned bit = 0; bit < 64; ++bit)
    if (mask & (uint64_t{1} << bit))
      names.emplace_back(av_get_channel_name(uint64_t{1} << bit));
#endif
  return names;
}

std::expected<Resampler::Context, ResamplerFailure>
Resampler::buildContext(const Configuration &configuration) {
  Context context(swr_alloc());
  if (!context)
    return std::unexpected(ResamplerFailure{ResamplerFailure::Kind::allocationFailed});
#if LIBAVUTIL_VERSION_MAJOR >= 57
  int result = setLayout(context.get(), "in_chlayout", inputLayout(configuration));
  if (result >= 0)
    result = setLayout(context.get(), "out_chlayout", convertedLayout(configuration));
#else
  int result = setLayout(context.get(), "in_channel_layout", inputLayout(configuration));
  if (result >= 0)
    result = setLayout(context.get(), "out_channel_layout", convertedLayout(configuration));
#endif
  if (result >= 0)
    result = av_opt_set_sample_fmt(context.get(), "in_sample_fmt", configuration.decoded, 0);
  if (result >= 0)
    result = av_opt_set_sample_fmt(context.get(), "out_sample_fmt",
                                  intermediateFormat(configuration.input), 0);
  if (result >= 0)
    result = av_opt_set_int(context.get(), "in_sample_rate", configuration.input.sampleRate(), 0);
  if (result >= 0)
    result = av_opt_set_int(context.get(), "out_sample_rate", configuration.output.rate, 0);
  if (result >= 0)
    result = swr_init(context.get());
  if (result < 0)
    return std::unexpected(ResamplerFailure{ResamplerFailure::Kind::initializationFailed, result});
  return context;
}

std::expected<ResamplerChange, ResamplerFailure>
Resampler::configure(AudioFormat input, AVSampleFormat decoded, OutputFormat output) {
  std::lock_guard lock(mutex_);
  if (output.rate == 0 || output.rate > INT_MAX || output.channels == 0 || output.channels > 8 ||
      av_get_bytes_per_sample(decoded) == 0)
    return std::unexpected(ResamplerFailure{ResamplerFailure::Kind::invalidFormat});
  Configuration replacement{input, decoded, std::move(output)};
  if (configuration_ == replacement)
    return ResamplerChange::unchanged;
  try {
    auto context = buildContext(replacement);
    if (!context)
      return std::unexpected(context.error());
    auto names = channelNames(convertedLayout(replacement));
    auto mapping = ChannelMapping::from(names, replacement.output.channels,
                                        replacement.output.mapping);
    convertedChannels_ = names.size();
    context_ = std::move(*context);
    mapping_ = std::move(mapping);
    configuration_ = std::move(replacement);
    retained_ = 0;
    return ResamplerChange::changed;
  } catch (const std::bad_alloc &) {
    return std::unexpected(ResamplerFailure{ResamplerFailure::Kind::allocationFailed});
  }
}

std::expected<ConvertedAudio, ResamplerFailure> Resampler::convert(const AVFrame &frame) {
  std::lock_guard lock(mutex_);
  if (!configuration_)
    return std::unexpected(ResamplerFailure{ResamplerFailure::Kind::notConfigured});
  const int channels =
#if LIBAVUTIL_VERSION_MAJOR >= 57
      frame.ch_layout.nb_channels;
#else
      frame.channels;
#endif
  if (frame.nb_samples < 0 || frame.format != configuration_->decoded ||
      channels != static_cast<int>(configuration_->input.channels()) ||
      frame.sample_rate != static_cast<int>(configuration_->input.sampleRate()))
    return std::unexpected(ResamplerFailure{ResamplerFailure::Kind::invalidFrame});
  std::array<const uint8_t *, 64> planes{};
  const unsigned count = av_sample_fmt_is_planar(configuration_->decoded)
                             ? configuration_->input.channels() : 1;
  for (unsigned index = 0; index < count; ++index) {
    if (frame.nb_samples != 0 && (!frame.extended_data || !frame.extended_data[index]))
      return std::unexpected(ResamplerFailure{ResamplerFailure::Kind::invalidFrame});
    planes[index] = frame.extended_data ? frame.extended_data[index] : nullptr;
  }
  return convertSamples(planes.data(), frame.nb_samples);
}

std::expected<ConvertedAudio, ResamplerFailure> Resampler::convertSamples(const uint8_t **planes,
                                                                       int frames) {
  const int capacity = swr_get_out_samples(context_.get(), frames);
  if (capacity < 0)
    return std::unexpected(ResamplerFailure{ResamplerFailure::Kind::conversionFailed, capacity});
  const unsigned bytesPerSample = av_get_bytes_per_sample(intermediateFormat(configuration_->input));
  const int scratchBytes = capacity == 0 ? 0 : av_samples_get_buffer_size(
      nullptr, convertedChannels_, capacity, intermediateFormat(configuration_->input), 0);
  if (scratchBytes < 0)
    return std::unexpected(ResamplerFailure{ResamplerFailure::Kind::allocationFailed, scratchBytes});
  auto scratch = ConvertedAudio::allocate(static_cast<size_t>(scratchBytes), 0, 0);
  if (!scratch)
    return std::unexpected(ResamplerFailure{ResamplerFailure::Kind::allocationFailed});
  uint8_t *buffer = scratch->bytes().data();
  const int generated = swr_convert(context_.get(), &buffer, capacity, planes, frames);
  if (generated < 0)
    return std::unexpected(ResamplerFailure{ResamplerFailure::Kind::conversionFailed, generated});
  retained_ = swr_get_delay(context_.get(), configuration_->output.rate);
  auto converted = ConvertedAudio::allocate(size_t(generated) * configuration_->output.channels *
                                              bytesPerSample, generated, retained_, producedShape());
  if (!converted)
    return std::unexpected(ResamplerFailure{ResamplerFailure::Kind::allocationFailed});
  const size_t sourceSamples = size_t(generated) * convertedChannels_;
  const size_t outputSamples = size_t(generated) * configuration_->output.channels;
  const bool mapped = bytesPerSample == 2
      ? mapping_.map({reinterpret_cast<const int16_t *>(buffer), sourceSamples},
                     {reinterpret_cast<int16_t *>(converted->bytes().data()), outputSamples})
      : mapping_.map({reinterpret_cast<const int32_t *>(buffer), sourceSamples},
                 {reinterpret_cast<int32_t *>(converted->bytes().data()), outputSamples});
  if (!mapped)
    return std::unexpected(ResamplerFailure{ResamplerFailure::Kind::invalidFormat});
  return std::move(*converted);
}

std::expected<ConvertedAudio, ResamplerFailure> Resampler::silence(size_t frames) {
  std::lock_guard lock(mutex_);
  if (!configuration_)
    return std::unexpected(ResamplerFailure{ResamplerFailure::Kind::notConfigured});
  if (frames > INT_MAX)
    return std::unexpected(ResamplerFailure{ResamplerFailure::Kind::invalidFrame});
  const int injected = swr_inject_silence(context_.get(), static_cast<int>(frames));
  if (injected < 0)
    return std::unexpected(ResamplerFailure{ResamplerFailure::Kind::conversionFailed, injected});
  std::array<const uint8_t *, 8> emptyPlanes{};
  return convertSamples(emptyPlanes.data(), 0);
}

std::expected<size_t, ResamplerFailure> Resampler::flush() {
  std::lock_guard lock(mutex_);
  if (!configuration_)
    return size_t{0};
  const int pending = swr_get_out_samples(context_.get(), 0);
  const int initialized = swr_init(context_.get());
  if (pending < 0 || initialized < 0)
    return std::unexpected(ResamplerFailure{ResamplerFailure::Kind::initializationFailed,
                                           pending < 0 ? pending : initialized});
  retained_ = 0;
  return static_cast<size_t>(pending);
}
void Resampler::reset() {
  std::lock_guard lock(mutex_);
  context_.reset();
  configuration_.reset();
  mapping_ = ChannelMapping{};
  convertedChannels_ = 0;
  retained_ = 0;
}
bool Resampler::configuredFor(AudioFormat format) const {
  std::lock_guard lock(mutex_);
  return configuration_ && configuration_->input == format;
}
unsigned Resampler::sampleBits() const {
  std::lock_guard lock(mutex_);
  return configuration_ ? av_get_bytes_per_sample(intermediateFormat(configuration_->input)) * 8 : 0;
}
unsigned Resampler::effectiveSampleBits() const {
  std::lock_guard lock(mutex_);
  if (!configuration_)
    return 0;
  return configuration_->input.isAac() ? 32 : configuration_->input.sampleBits();
}
int64_t Resampler::retainedFrames() const {
  std::lock_guard lock(mutex_);
  return retained_;
}
NativePcmShape Resampler::producedShape() const {
  if (!configuration_)
    return {};
  return {configuration_->output.channels,
          static_cast<unsigned>(av_get_bytes_per_sample(intermediateFormat(configuration_->input))) * 8,
          configuration_->input.isAac() ? 32 : configuration_->input.sampleBits()};
}
NativePcmShape Resampler::outputShape() const {
  std::lock_guard lock(mutex_);
  return producedShape();
}
