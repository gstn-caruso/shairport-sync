#pragma once

#include "app/receiver_settings.hpp"
#include "app/startup_options.hpp"
#include <expected>
#include <string>

class ConfigurationLoader {
public:
  static std::expected<ReceiverSettings, std::string> load(const StartupOptions &options);
};
