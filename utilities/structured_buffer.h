#ifndef _STRUCTURED_BUFFER_H
#define _STRUCTURED_BUFFER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct structured_buffer structured_buffer;

structured_buffer *sbuf_new(size_t size);
int sbuf_clear(structured_buffer *sbuf);
void sbuf_free(structured_buffer *sbuf);
void sbuf_cleanup(void *arg);
int sbuf_printf(structured_buffer *sbuf, const char *format, ...);
int sbuf_append(structured_buffer *sbuf, char *plistString, uint32_t plistStringLength);
int sbuf_buf_and_length(structured_buffer *sbuf, char **b, size_t *l);

#ifdef __cplusplus
}
#endif

#endif // _STRUCTURED_BUFFER_H
