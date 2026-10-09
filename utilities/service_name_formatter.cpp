#include "string_utilities.hpp"
#include <utility>

namespace shairport {

ServiceNameFormatter::ServiceNameFormatter(std::string hostname, std::string packageVersion,
                                         std::string detailedVersion)
    : hostname_(std::move(hostname)), packageVersion_(std::move(packageVersion)),
      detailedVersion_(std::move(detailedVersion)) {}

std::string ServiceNameFormatter::format() const {
  std::string name = hostname_.substr(0, hostname_.rfind('.'));
  if (!name.empty() && name.front() >= 'a' && name.front() <= 'z')
    name.front() -= 'a' - 'A';
  return name;
}

}
