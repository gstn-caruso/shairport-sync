#pragma once

#include <libconfig.h>
#include <expected>
#include <string>

std::expected<void, std::string> validateConfigurationTypes(const config_t &settings);
