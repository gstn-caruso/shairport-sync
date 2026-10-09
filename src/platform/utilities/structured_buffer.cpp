/*
 * Structured Buffer. This file is part of Shairport Sync
 * Copyright (c) Mike Brady 2025
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

#include "platform/utilities/structured_buffer.h"
#include "platform/utilities/structured_buffer.hpp"
#include "platform/utilities/debug.h"
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

StructuredBuffer::StructuredBuffer(std::size_t capacity)
    : capacity_(capacity), position_(0) {
  if (capacity == std::numeric_limits<std::size_t>::max())
    throw std::length_error("structured buffer capacity cannot include its extra byte");
  bytes_.reset(new char[capacity + 1]);
}

void StructuredBuffer::clear() noexcept { position_ = 0; }

int StructuredBuffer::append_format(const char *format, std::va_list args) noexcept {
  if (position_ == capacity_)
    return 0;
  char *destination = bytes_.get() + position_;
  std::vsnprintf(destination, capacity_ - position_, format, args);
  std::size_t written = std::strlen(destination);
  position_ += written;
  return static_cast<int>(written);
}

int StructuredBuffer::append(const char *bytes, std::uint32_t length) noexcept {
  if (length == 0)
    return 0;
  if (length >= capacity_ - position_)
    return -1;
  std::memcpy(bytes_.get() + position_, bytes, length);
  position_ += length;
  return 0;
}

char *StructuredBuffer::data() noexcept { return bytes_.get(); }
std::size_t StructuredBuffer::length() const noexcept { return position_; }

struct structured_buffer {
  explicit structured_buffer(size_t capacity) : value(capacity) {}
  StructuredBuffer value;
};

structured_buffer *sbuf_new(size_t size) {
  try {
    return new structured_buffer(size);
  } catch (...) {
    return nullptr;
  }
}

int sbuf_clear(structured_buffer *sbuf) {
  if (sbuf == nullptr)
    return -1;
  sbuf->value.clear();
  return 0;
}

void sbuf_free(structured_buffer *sbuf) {
  delete sbuf;
}

void sbuf_cleanup(void *arg) {
  structured_buffer *sbuf = static_cast<structured_buffer *>(arg);
  debug(3, "structured_buffer cleanup");
  sbuf_free(sbuf);
}

int sbuf_printf(structured_buffer *sbuf, const char *format, ...) {
  if (sbuf == nullptr || format == nullptr)
    return -1;
  va_list args;
  va_start(args, format);
  int response = sbuf->value.append_format(format, args);
  va_end(args);
  return response;
}

int sbuf_append(structured_buffer *sbuf, char *plistString, uint32_t plistStringLength) {
  if (sbuf == nullptr || plistString == nullptr)
    return -1;
  int response = sbuf->value.append(plistString, plistStringLength);
  if (response == -1)
    debug(1, "plist too large -- omitted");
  return response;
}

int sbuf_buf_and_length(structured_buffer *sbuf, char **b, size_t *l) {
  if (sbuf == nullptr || b == nullptr || l == nullptr)
    return -1;
  *b = sbuf->value.data();
  *l = sbuf->value.length();
  return 0;
}
