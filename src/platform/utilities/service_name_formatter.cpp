#include "platform/utilities/string_utilities.hpp"
#include <utility>

namespace shairport {

std::string replaceOccurrences(std::string_view text, std::string_view token,
                               std::string_view replacement) {
  std::string result(text);
  if (token.empty())
    return result;
  std::size_t position = 0;
  while ((position = result.find(token, position)) != std::string::npos) {
    result.replace(position, token.size(), replacement);
    position += replacement.size();
  }
  return result;
}

std::expected<std::string, TruncationError> appendWithLimit(std::string_view base,
                                                          std::string_view suffix,
                                                          std::size_t limit) {
  if (suffix.size() <= limit && base.size() <= limit - suffix.size())
    return std::string(base).append(suffix);
  constexpr std::string_view ellipsis = "...";
  if (suffix.size() > limit || ellipsis.size() > limit - suffix.size())
    return std::unexpected(TruncationError::limitTooSmall);
  std::size_t prefixLength = limit - ellipsis.size() - suffix.size();
  while (prefixLength > 0 && (base[prefixLength] & 0xC0) == 0x80)
    --prefixLength;
  return std::string(base.substr(0, prefixLength)).append(ellipsis).append(suffix);
}

ServiceNameFormatter::ServiceNameFormatter(std::string hostname, std::string packageVersion,
                                         std::string detailedVersion)
    : hostname_(std::move(hostname)), packageVersion_(std::move(packageVersion)),
      detailedVersion_(std::move(detailedVersion)) {}

std::string ServiceNameFormatter::format(std::string_view pattern) const {
  const std::string hostname = hostname_.substr(0, hostname_.rfind('.'));
  std::string capitalizedHostname = hostname;
  if (!capitalizedHostname.empty() && capitalizedHostname.front() >= 'a' &&
      capitalizedHostname.front() <= 'z')
    capitalizedHostname.front() -= 'a' - 'A';
  std::string name = replaceOccurrences(pattern, "%h", hostname);
  name = replaceOccurrences(name, "%H", capitalizedHostname);
  name = replaceOccurrences(name, "%v", packageVersion_);
  name = replaceOccurrences(name, "%V", detailedVersion_);
  constexpr std::size_t maxAirplayServiceNameLength = 50;
  return appendWithLimit(name, "", maxAirplayServiceNameLength).value();
}

}
