#pragma once

#include <string>
#include <string_view>
#include <expected>
#include <cstddef>

namespace shairport {

enum class TruncationError { limitTooSmall };

std::string replaceOccurrences(std::string_view text, std::string_view token,
                               std::string_view replacement);
std::expected<std::string, TruncationError> appendWithLimit(std::string_view base,
                                                          std::string_view suffix,
                                                          std::size_t limit);

class ServiceNameFormatter {
public:
  ServiceNameFormatter(std::string hostname, std::string packageVersion,
                       std::string detailedVersion);
  std::string format(std::string_view pattern = "%H") const;

private:
  std::string hostname_;
  std::string packageVersion_;
  std::string detailedVersion_;
};

}
