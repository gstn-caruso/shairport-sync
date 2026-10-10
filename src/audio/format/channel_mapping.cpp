#include "audio/format/channel_mapping.hpp"
#include <algorithm>
#include <sstream>

std::vector<std::string> ChannelMapping::Specification::requestedNames() const {
  auto requested = enabled ? names : std::vector<std::string>{};
  if (enabled && requested.empty()) {
    std::istringstream device(deviceNames);
    for (std::string name; device >> name;)
      requested.push_back(std::move(name));
  }
  return requested;
}

ChannelMapping ChannelMapping::from(std::vector<std::string> sourceNames, unsigned outputChannels,
                                    const Specification &specification) {
  ChannelMapping mapping;
  mapping.sourceChannels_ = sourceNames.size();
  mapping.selections_.resize(outputChannels);
  const auto names = specification.requestedNames();
  std::vector<bool> used(sourceNames.size());
  const auto assigned = mapping.selectRequestedChannels(sourceNames, names, used);
  mapping.fillUnassignedChannels(assigned, used);
  return mapping;
}

std::vector<bool> ChannelMapping::selectRequestedChannels(std::span<const std::string> sourceNames,
                                                         std::span<const std::string> requestedNames,
                                                         std::vector<bool> &used) {
  std::vector<bool> assigned(selections_.size());
  for (size_t index = 0; index < selections_.size() && index < requestedNames.size(); ++index) {
    if (requestedNames[index] == "--") {
      assigned[index] = true;
      continue;
    }
    if (requestedNames[index] == "FM" && sourceNames.size() >= 2) {
      selections_[index] = {Source::frontMono, 0};
      assigned[index] = true;
      continue;
    }
    auto found = std::ranges::find(sourceNames, requestedNames[index]);
    if (found != sourceNames.end()) {
      const auto channel = static_cast<unsigned>(found - sourceNames.begin());
      selections_[index] = {Source::channel, channel};
      used[channel] = true;
      assigned[index] = true;
    } else {
      incomplete_ = true;
    }
  }
  return assigned;
}

void ChannelMapping::fillUnassignedChannels(const std::vector<bool> &assigned, std::vector<bool> &used) {
  size_t source = 0;
  for (size_t output = 0; output < selections_.size(); ++output) {
    if (assigned[output])
      continue;
    while (source < used.size() && used[source])
      ++source;
    if (source == used.size())
      break;
    selections_[output] = {Source::channel, static_cast<unsigned>(source)};
    used[source++] = true;
  }
}
