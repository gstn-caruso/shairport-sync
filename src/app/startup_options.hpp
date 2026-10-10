#pragma once

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

class StartupOptions {
public:
  enum class Operation { receive, checkConfiguration, version };

  static std::expected<StartupOptions, std::string>
  parse(std::span<const std::string_view> arguments);

  Operation operation() const { return operation_; }
  const std::optional<std::string> &configurationPath() const { return configurationPath_; }

private:
  Operation operation_ = Operation::receive;
  std::optional<std::string> configurationPath_;
};
