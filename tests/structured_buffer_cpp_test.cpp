#include "utilities/structured_buffer.hpp"
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

static int format_into(StructuredBuffer &buffer, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = buffer.append_format(format, args);
  va_end(args);
  return result;
}

int main() {
  static_assert(!std::is_copy_constructible<StructuredBuffer>::value, "unique ownership");
  static_assert(!std::is_copy_assignable<StructuredBuffer>::value, "unique ownership");
  StructuredBuffer buffer(8);
  assert(buffer.length() == 0);
  assert(format_into(buffer, "%s", "hello") == 5);
  const char binary[] = {0, 'x'};
  assert(buffer.append(binary, sizeof(binary)) == 0);
  assert(buffer.length() == 7);
  assert(std::memcmp(buffer.data(), "hello\0x", 7) == 0);
  assert(buffer.append("x", 1) == -1);
  assert(format_into(buffer, "%s", "full") == 0);
  buffer.clear();
  assert(buffer.length() == 0);
  assert(std::memcmp(buffer.data(), "hello\0x", 7) == 0);
  assert(format_into(buffer, "%c%s", 0, "tail") == 0);
  assert(format_into(buffer, "%s", "123456789") == 7);
  assert(buffer.length() == 7);
  StructuredBuffer empty(0);
  empty.data()[0] = 'x';
  assert(format_into(empty, "%s", "text") == 0);
  assert(empty.length() == 0 && empty.data()[0] == 'x');
  bool rejected = false;
  try {
    StructuredBuffer impossible(std::numeric_limits<std::size_t>::max());
  } catch (const std::length_error &) {
    rejected = true;
  }
  assert(rejected);
  return 0;
}
