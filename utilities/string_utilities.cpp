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
#include <string>
#include <unistd.h>

#include "config.h"
#include "string_utilities.h"
#include "string_utilities.hpp"

// common.h exposes C-only restrict declarations; this result is owned with free().
extern "C" char *get_version_string();

namespace {

char *malloc_copy(const std::string &text) {
  char *copy = static_cast<char *>(std::malloc(text.size() + 1));
  if (copy != nullptr)
    std::memcpy(copy, text.c_str(), text.size() + 1);
  return copy;
}

}

char *str_replace(const char *string, const char *substr, const char *replacement) {
  try {
    if (string == nullptr)
      return nullptr;
    if (substr != nullptr && replacement != nullptr)
      return malloc_copy(shairport::replaceOccurrences(string, substr, replacement));
    return malloc_copy(string);
  } catch (...) {
    return nullptr;
  }
}

char *append_truncated(const char *base, const char *suffix, size_t limit) {
  try {
    if (base == nullptr || suffix == nullptr)
      return nullptr;
    const auto result = shairport::appendWithLimit(base, suffix, limit);
    return result ? malloc_copy(*result) : nullptr;
  } catch (...) {
    return nullptr;
  }
}

char *service_name(const char *raw_service_name) {
  try {
    char hostname[256] = {};
    gethostname(hostname, sizeof(hostname));
    hostname[sizeof(hostname) - 1] = '\0';
    std::unique_ptr<char, decltype(&std::free)> version(get_version_string(), &std::free);
    if (version == nullptr)
      return nullptr;
    const shairport::ServiceNameFormatter formatter(hostname, PACKAGE_VERSION, version.get());
    return malloc_copy(formatter.format(raw_service_name != nullptr ? raw_service_name : "%H"));
  } catch (...) {
    return nullptr;
  }
}
