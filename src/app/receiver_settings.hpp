#pragma once

#include "runtime/common.h"

class ConfigurationLoader;
class ReceiverApplication;

class ReceiverSettings {
public:
  ReceiverSettings(const ReceiverSettings &) = delete;
  ReceiverSettings &operator=(const ReceiverSettings &) = delete;
  ReceiverSettings(ReceiverSettings &&) = default;

private:
  friend class ConfigurationLoader;
  friend class ReceiverApplication;
  explicit ReceiverSettings(const shairport_cfg &values) : values_(values) {}
  const shairport_cfg values_;
};
