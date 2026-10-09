#pragma once

#include "audio_types.h"
#include <optional>
#include <string_view>

class AudioFormat {
public:
  static std::optional<AudioFormat> fromSsrc(ssrc_t);
  ssrc_t ssrc() const { return ssrc_; }
  bool isAac() const { return aac_; }
  unsigned sampleRate() const { return rate_; }
  unsigned channels() const { return channels_; }
  unsigned framesPerPacket() const { return aac_ ? 1024 : 352; }
  unsigned sampleBits() const { return sampleFormat_ == SPS_FORMAT_S16 ? 16 : 24; }
  sps_format_t suggestedSampleFormat() const { return sampleFormat_; }
  std::string_view name() const { return name_; }
  bool operator==(const AudioFormat &) const = default;

private:
  AudioFormat(ssrc_t ssrc, bool aac, unsigned rate, unsigned channels,
              sps_format_t sampleFormat, std::string_view name)
      : ssrc_(ssrc), aac_(aac), rate_(rate), channels_(channels),
        sampleFormat_(sampleFormat), name_(name) {}
  ssrc_t ssrc_;
  bool aac_;
  unsigned rate_, channels_;
  sps_format_t sampleFormat_;
  std::string_view name_;
};
