#pragma once

#include "audio_format.hpp"
#include "channel_mapping.hpp"
#include "converted_audio.hpp"
#include <expected>
#include <mutex>

struct OutputFormat {
  unsigned rate, channels;
  uint64_t inputLayout = 0;
  bool mixdown = false;
  uint64_t mixdownLayout = 0;
  ChannelMapping::Specification mapping{};
  bool operator==(const OutputFormat &) const = default;
};
enum class ResamplerChange { changed, unchanged };
struct ResamplerFailure {
  enum class Kind { notConfigured, invalidFormat, invalidFrame, allocationFailed,
                    initializationFailed, conversionFailed };
  Kind kind;
  int nativeCode = 0;
};

class Resampler {
public:
  std::expected<ResamplerChange, ResamplerFailure> configure(AudioFormat, AVSampleFormat,
                                                            OutputFormat);
  std::expected<ConvertedAudio, ResamplerFailure> convert(const AVFrame &);
  std::expected<ConvertedAudio, ResamplerFailure> silence(size_t frames);
  std::expected<size_t, ResamplerFailure> flush();
  void reset();
  bool configuredFor(AudioFormat) const;
  unsigned sampleBits() const;
  unsigned effectiveSampleBits() const;
  int64_t retainedFrames() const;
  NativePcmShape outputShape() const;

private:
  struct Configuration {
    AudioFormat input;
    AVSampleFormat decoded;
    OutputFormat output;
    bool operator==(const Configuration &) const = default;
  };
  struct ContextDeleter { void operator()(SwrContext *context) const { swr_free(&context); } };
  using Context = std::unique_ptr<SwrContext, ContextDeleter>;
  static uint64_t inputLayout(const Configuration &);
  static uint64_t convertedLayout(const Configuration &);
  static std::vector<std::string> channelNames(uint64_t mask);
  static AVSampleFormat intermediateFormat(AudioFormat);
  static std::expected<Context, ResamplerFailure> buildContext(const Configuration &);
  std::expected<ConvertedAudio, ResamplerFailure> convertSamples(const uint8_t **, int frames);
  NativePcmShape producedShape() const;
  mutable std::mutex mutex_;
  Context context_;
  std::optional<Configuration> configuration_;
  ChannelMapping mapping_;
  unsigned convertedChannels_ = 0;
  int64_t retained_ = 0;
};
