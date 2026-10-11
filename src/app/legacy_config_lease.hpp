#pragma once
#include "app/receiver_settings.hpp"

class LegacyConfigLease {
public:
  explicit LegacyConfigLease(ReceiverSettings settings);
  LegacyConfigLease(const LegacyConfigLease &) = delete;
  LegacyConfigLease &operator=(const LegacyConfigLease &) = delete;
  const ReceiverSettings &settings() const { return settings_; }
  char *configurationRealPath();

private:
  ReceiverSettings settings_;
};
