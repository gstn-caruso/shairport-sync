#ifndef STRUCTURED_BUFFER_HPP
#define STRUCTURED_BUFFER_HPP

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <memory>

/*
 * I am a fixed-capacity buffer for building messages from text and binary bytes.
 * I own my storage and know how much of it belongs to the current message.
 * Ask me to append bytes, append formatted text, or clear the message for reuse.
 * I reject binary additions that leave no room for a terminator; formatted text
 * is truncated to fit. Clearing retains my storage, and destruction releases it.
 * My C adapter borrows data() and length() to send the message; the borrowed
 * storage remains valid until my destruction, and later writes change its bytes.
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
