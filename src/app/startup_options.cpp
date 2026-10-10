#include "app/startup_options.hpp"

std::expected<StartupOptions, std::string>
StartupOptions::parse(std::span<const std::string_view> arguments) {
  StartupOptions options;
  if (arguments.size() == 1 && arguments.front() == "--version") {
    options.operation_ = Operation::version;
    return options;
  }
  for (auto argument = arguments.begin(); argument != arguments.end(); ++argument) {
    if (*argument == "--check-config" && options.operation_ == Operation::receive) {
      options.operation_ = Operation::checkConfiguration;
    } else if (*argument == "--config" && !options.configurationPath_) {
      ++argument;
      if (argument == arguments.end() || argument->empty() || argument->starts_with("--"))
        return std::unexpected("--config requires a file path");
      options.configurationPath_ = std::string(*argument);
    } else {
      return std::unexpected("Invalid or repeated argument: " + std::string(*argument));
    }
  }
  return options;
}
