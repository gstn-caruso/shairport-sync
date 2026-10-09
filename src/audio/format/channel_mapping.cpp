#include "audio/format/channel_mapping.hpp"
#include <algorithm>
#include <sstream>

ChannelMapping ChannelMapping::from(std::vector<std::string> sourceNames, unsigned outputChannels,
                                    const Specification &specification) {
  ChannelMapping mapping;
  mapping.sourceChannels_ = sourceNames.size();
  mapping.selections_.resize(outputChannels);
  auto names = specification.enabled ? specification.names : std::vector<std::string>{};
  if (specification.enabled && names.empty()) {
    std::istringstream device(specification.deviceNames);
    for (std::string name; device >> name;)
      names.push_back(std::move(name));
  }
  std::vector<bool> used(sourceNames.size());
  std::vector<bool> assigned(outputChannels);
  for (size_t index = 0; index < outputChannels && index < names.size(); ++index) {
    if (names[index] == "--") {
      assigned[index] = true;
      continue;
    }
    if (names[index] == "FM" && sourceNames.size() >= 2) {
      mapping.selections_[index] = {Source::frontMono, 0};
      assigned[index] = true;
      continue;
    }
    auto found = std::ranges::find(sourceNames, names[index]);
    if (found != sourceNames.end()) {
      const auto channel = static_cast<unsigned>(found - sourceNames.begin());
      mapping.selections_[index] = {Source::channel, channel};
      used[channel] = true;
      assigned[index] = true;
    } else {
      mapping.incomplete_ = true;
    }
  }
  size_t source = 0;
  for (size_t output = 0; output < outputChannels; ++output) {
    if (assigned[output])
      continue;
    while (source < used.size() && used[source])
      ++source;
    if (source == used.size())
      break;
    mapping.selections_[output] = {Source::channel, static_cast<unsigned>(source)};
    used[source++] = true;
  }
  return mapping;
}
