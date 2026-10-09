#pragma once

#include <string>

namespace shairport {

class ServiceNameFormatter {
public:
  ServiceNameFormatter(std::string hostname, std::string packageVersion,
                       std::string detailedVersion);
  std::string format() const;

private:
  std::string hostname_;
  std::string packageVersion_;
  std::string detailedVersion_;
};

}
