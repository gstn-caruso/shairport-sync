#include "audio/output/output_setup.hpp"
#include "audio/format/encoded_output_format.h"
#include <utility>
extern "C" {
#include <libavutil/channel_layout.h>
}

OutputSetupCoordinator::OutputSetupCoordinator(OutputSetupSettings settings, OutputSetupPort &port,
                                               Resampler &resampler, PcmEncoder &pcm,
                                               Publication afterResamplerConfigured)
    : settings_(std::move(settings)), port_(port), resampler_(resampler), pcm_(pcm),
      afterResamplerConfigured_(std::move(afterResamplerConfigured)) {}

std::expected<uint32_t, OutputSetupFailure> OutputSetupCoordinator::configure(
    AudioFormat input, AVSampleFormat explicitDecodedFormat,
    std::optional<AVSampleFormat> decoderFormat) {
  constexpr uint32_t defaultOutput = CHANNELS_TO_ENCODED_FORMAT(2) | RATE_TO_ENCODED_FORMAT(48000) |
                                     FORMAT_TO_ENCODED_FORMAT(SPS_FORMAT_S32_LE);
  const auto encoded = port_.choose(input).value_or(defaultOutput);
  if (encoded == 0)
    return std::unexpected(OutputSetupFailure{OutputSetupFailure::Kind::backendRejected, {}});
  auto deviceNames = port_.configure(encoded);
  OutputFormat output{RATE_FROM_ENCODED_FORMAT(encoded), CHANNELS_FROM_ENCODED_FORMAT(encoded)};
  output.inputLayout = input.channels() == 6 ? settings_.sixChannelLayout :
                       input.channels() == 8 ? settings_.eightChannelLayout : AV_CH_LAYOUT_STEREO;
  output.mixdown = settings_.mixdown;
  output.mixdownLayout = settings_.mixdownLayout;
  output.mapping = {settings_.mappingEnabled, settings_.channelNames, std::move(deviceNames)};
  const auto decoded = explicitDecodedFormat != AV_SAMPLE_FMT_NONE ? explicitDecodedFormat :
                       input.isAac() ? AV_SAMPLE_FMT_FLTP : decoderFormat.value_or(AV_SAMPLE_FMT_FLTP);
  auto configured = resampler_.configure(input, decoded, std::move(output));
  if (!configured)
    return std::unexpected(OutputSetupFailure{OutputSetupFailure::Kind::resamplerFailed,
                                             configured.error()});
  afterResamplerConfigured_(encoded, input);
  const auto shape = resampler_.outputShape();
  if (!pcm_.configure({FORMAT_FROM_ENCODED_FORMAT(encoded), shape.channels()}, shape.effectiveBits()))
    return std::unexpected(OutputSetupFailure{OutputSetupFailure::Kind::unsupportedPcm, {}});
  return encoded;
}
