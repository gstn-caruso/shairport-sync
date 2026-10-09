#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returned strings belong to the caller and must be released with free().
 * Allocation failures return NULL.
 * An empty or NULL replacement token leaves the input unchanged.
 */
char *str_replace(const char *string, const char *substr, const char *replacement);

/* Append suffix within limit bytes, truncating base at a UTF-8 boundary with
 * "..." if needed. Return NULL if the limit cannot fit the ellipsis and suffix.
 */
char *append_truncated(const char *base, const char *suffix, size_t limit);

char *service_name(const char *raw_service_name);

#ifdef __cplusplus
}
#endif
