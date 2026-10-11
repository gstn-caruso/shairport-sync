#pragma once

#include "app/receiver_settings.hpp"
#include "app/startup_options.hpp"
#include <expected>
#include <string>
#include <array>
#include <functional>

struct ConfigurationEnvironment {
  std::string defaultPath;
  std::string hostname;
  std::string packageVersion;
  std::string detailedVersion;
  std::string firmwareVersion;
  std::array<uint8_t, 6> hardwareAddress{};
  int endianness = SS_LITTLE_ENDIAN;
  std::string timingInterfaceName = "/nqptp";
  std::function<unsigned(std::string_view)> interfaceIndex;
};

struct ConfigurationFailure {
  std::string message;
  std::vector<ConfigurationDiagnostic> precedingDiagnostics;
};

class ConfigurationLoader {
public:
  static std::expected<ReceiverSettings, ConfigurationFailure>
  load(const StartupOptions &options, const ConfigurationEnvironment &environment);
};
