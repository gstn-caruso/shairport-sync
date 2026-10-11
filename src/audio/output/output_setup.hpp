#pragma once
#include "audio/pcm/pcm_encoder.hpp"
#include "audio/resampling/resampler.hpp"
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct OutputSetupSettings {
  uint64_t sixChannelLayout = 0;
  uint64_t eightChannelLayout = 0;
  bool mixdown = false;
  uint64_t mixdownLayout = 0;
  bool mappingEnabled = false;
  std::vector<std::string> channelNames;
};

class OutputSetupPort {
public:
  virtual ~OutputSetupPort() = default;
  virtual std::optional<uint32_t> choose(AudioFormat input) = 0;
  virtual std::string configure(uint32_t encoded) = 0;
};

struct OutputSetupFailure {
  enum class Kind { backendRejected, resamplerFailed, unsupportedPcm };
  Kind kind;
  std::optional<ResamplerFailure> resamplerFailure;
};

class OutputSetupCoordinator {
public:
  using Publication = std::function<void(uint32_t encoded, AudioFormat input)>;
  OutputSetupCoordinator(OutputSetupSettings settings, OutputSetupPort &port, Resampler &resampler,
                         PcmEncoder &pcm, Publication afterResamplerConfigured);
  std::expected<uint32_t, OutputSetupFailure> configure(
      AudioFormat input, AVSampleFormat explicitDecodedFormat = AV_SAMPLE_FMT_NONE,
      std::optional<AVSampleFormat> decoderFormat = std::nullopt);

private:
  OutputSetupSettings settings_;
  OutputSetupPort &port_;
  Resampler &resampler_;
  PcmEncoder &pcm_;
  Publication afterResamplerConfigured_;
};
