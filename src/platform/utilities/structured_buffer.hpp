#ifndef STRUCTURED_BUFFER_HPP
#define STRUCTURED_BUFFER_HPP

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <memory>

/*
 * Binary appends must leave room for a terminator; formatted text is truncated.
 * data() returns borrowed storage valid until destruction. Later writes change
 * its contents; clear() retains the storage.
 */
class StructuredBuffer {
public:
  explicit StructuredBuffer(std::size_t capacity);
  StructuredBuffer(const StructuredBuffer &) = delete;
  StructuredBuffer &operator=(const StructuredBuffer &) = delete;
  void clear() noexcept;
  int append_format(const char *format, std::va_list args) noexcept;
  int append(const char *bytes, std::uint32_t length) noexcept;
  char *data() noexcept;
  std::size_t length() const noexcept;

private:
  std::unique_ptr<char[]> bytes_;
  const std::size_t capacity_;
  std::size_t position_;
};

#endif
