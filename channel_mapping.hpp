#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

class ChannelMapping {
public:
  struct Specification {
    bool enabled = false;
    std::vector<std::string> names;
    std::string deviceNames;
    bool operator==(const Specification &) const = default;
  };
  static ChannelMapping from(std::vector<std::string> sourceNames, unsigned outputChannels,
                              const Specification &specification);
  bool isIncomplete() const { return incomplete_; }
  bool map(std::span<const int16_t> input, std::span<int16_t> output) const {
    return mapSamples(input, output);
  }
  bool map(std::span<const int32_t> input, std::span<int32_t> output) const {
    return mapSamples(input, output);
  }

private:
  enum class Source { channel, silence, frontMono };
  struct Selection { Source source = Source::silence; unsigned channel = 0; };
  template <typename Sample>
  bool mapSamples(std::span<const Sample> input, std::span<Sample> output) const {
    if (sourceChannels_ == 0 || selections_.empty() || input.size() % sourceChannels_ != 0 ||
        output.size() != input.size() / sourceChannels_ * selections_.size())
      return false;
    for (size_t frame = 0; frame < input.size() / sourceChannels_; ++frame)
      for (size_t channel = 0; channel < selections_.size(); ++channel) {
        const auto &selection = selections_[channel];
        const auto source = input.subspan(frame * sourceChannels_, sourceChannels_);
        Sample value = 0;
        if (selection.source == Source::channel)
          value = source[selection.channel];
        else if (selection.source == Source::frontMono)
          value = source[0] / 2 + source[1] / 2;
        output[frame * selections_.size() + channel] = value;
      }
    return true;
  }
  unsigned sourceChannels_ = 0;
  std::vector<Selection> selections_;
  bool incomplete_ = false;
};
