/*
 * Some character string utilities. This file is part of Shairport Sync
 * Copyright (c) Mike Brady 2026
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include "config.h"
#include "string_utilities.h"

// common.h exposes C-only restrict declarations; this result is owned with free().
extern "C" char *get_version_string();

namespace {

// DNS-SD labels allow 63 bytes; the AirPlay prefix uses 13.
constexpr size_t max_airplay_service_name_length = 50;

char *malloc_copy(const std::string &text) {
  char *copy = static_cast<char *>(std::malloc(text.size() + 1));
  if (copy != nullptr)
    std::memcpy(copy, text.c_str(), text.size() + 1);
  return copy;
}

void replace_occurrences(std::string &text, const std::string &token,
                         const std::string &replacement) {
  size_t position = 0;
  while ((position = text.find(token, position)) != std::string::npos) {
    text.replace(position, token.size(), replacement);
    position += replacement.size();
  }
}

std::string append_with_limit(const std::string &base, const std::string &suffix,
                             size_t limit) {
  if (suffix.size() <= limit && base.size() <= limit - suffix.size())
    return base + suffix;

  const std::string ellipsis = "...";
  if (suffix.size() > limit || ellipsis.size() > limit - suffix.size())
    throw std::length_error("limit cannot fit ellipsis and suffix");

  size_t prefix_length = limit - ellipsis.size() - suffix.size();
  while (prefix_length > 0 && (base[prefix_length] & 0xC0) == 0x80)
    --prefix_length;
  return base.substr(0, prefix_length) + ellipsis + suffix;
}

std::string hostname_without_domain() {
  char hostname[256] = {};
  gethostname(hostname, sizeof(hostname));
  hostname[sizeof(hostname) - 1] = '\0';
  std::string name(hostname);
  size_t last_dot = name.rfind('.');
  if (last_dot != std::string::npos)
    name.erase(last_dot);
  return name;
}

std::string capitalized_hostname(std::string hostname) {
  if (!hostname.empty() && hostname[0] >= 'a' && hostname[0] <= 'z')
    hostname[0] -= 'a' - 'A';
  return hostname;
}

std::string expanded_service_name(const char *raw_service_name) {
  std::string name(raw_service_name != nullptr ? raw_service_name : "%H");
  const std::string hostname = hostname_without_domain();
  replace_occurrences(name, "%h", hostname);
  replace_occurrences(name, "%H", capitalized_hostname(hostname));
  replace_occurrences(name, "%v", PACKAGE_VERSION);
  std::unique_ptr<char, decltype(&std::free)> version(get_version_string(), &std::free);
  if (version == nullptr)
    throw std::bad_alloc();
  replace_occurrences(name, "%V", version.get());
  return name;
}

}

char *str_replace(const char *string, const char *substr, const char *replacement) {
  try {
    std::string text(string);
    if (substr != nullptr && replacement != nullptr)
      replace_occurrences(text, substr, replacement);
    return malloc_copy(text);
  } catch (...) {
    return nullptr;
  }
}

char *append_truncated(const char *base, const char *suffix, size_t limit) {
  try {
    return malloc_copy(append_with_limit(base, suffix, limit));
  } catch (...) {
    return nullptr;
  }
}

char *service_name(const char *raw_service_name) {
  try {
    return malloc_copy(append_with_limit(expanded_service_name(raw_service_name), "",
                                       max_airplay_service_name_length));
  } catch (...) {
    return nullptr;
  }
}
