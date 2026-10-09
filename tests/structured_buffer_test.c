#include <stddef.h>
#include <stdint.h>
#include "utilities/structured_buffer.h"
#include <assert.h>
#include <string.h>

static void check_formatting_and_binary_append(void) {
  structured_buffer *buffer = sbuf_new(32);
  assert(buffer != NULL);
  char *bytes;
  size_t length;
  assert(sbuf_buf_and_length(buffer, &bytes, &length) == 0);
  assert(length == 0);
  assert(sbuf_printf(buffer, "%s %u", "POST", 7U) == 6);
  char binary[] = {'\0', 'x', '\0'};
  assert(sbuf_append(buffer, binary, sizeof(binary)) == 0);
  assert(sbuf_buf_and_length(buffer, &bytes, &length) == 0);
  assert(length == 9);
  assert(memcmp(bytes, "POST 7", 6) == 0);
  assert(memcmp(bytes + 6, binary, sizeof(binary)) == 0);
  assert(sbuf_clear(buffer) == 0);
  assert(sbuf_buf_and_length(buffer, &bytes, &length) == 0);
  assert(length == 0);
  assert(memcmp(bytes, "POST 7", 6) == 0);
  assert(sbuf_printf(buffer, "%c%s", 0, "ignored") == 0);
  assert(sbuf_printf(buffer, "%s", "reuse") == 5);
  sbuf_cleanup(buffer);
}

static void check_capacity_and_invalid_handles(void) {
  structured_buffer *buffer = sbuf_new(5);
  char data[] = "12345";
  assert(sbuf_append(buffer, data, 5) == -1);
  assert(sbuf_append(buffer, data, 0) == 0);
  assert(sbuf_append(buffer, NULL, 0) == -1);
  assert(sbuf_printf(buffer, "%s", data) == 4);
  assert(sbuf_printf(buffer, "%s", "more") == 0);
  char *bytes;
  size_t length;
  assert(sbuf_buf_and_length(buffer, &bytes, &length) == 0);
  assert(length == 4 && memcmp(bytes, "1234", 4) == 0);
  assert(sbuf_append(buffer, data, 1) == -1);
  sbuf_free(buffer);
  assert(sbuf_clear(NULL) == -1);
  assert(sbuf_printf(NULL, "%s", data) == -1);
  assert(sbuf_append(NULL, data, 1) == -1);
  assert(sbuf_buf_and_length(NULL, &bytes, &length) == -1);
  sbuf_free(NULL);
  sbuf_cleanup(NULL);
}

int main(void) {
  check_formatting_and_binary_append();
  check_capacity_and_invalid_handles();
  return 0;
}
