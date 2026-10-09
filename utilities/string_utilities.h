#pragma once

#include <stddef.h>

/* from
 * http://coding.debuntu.org/c-implementing-str_replace-replace-all-occurrences-substring#comment-722
 */
char *str_replace(const char *string, const char *substr, const char *replacement);

char *append_truncated(const char *base, const char *suffix, size_t limit);

char *service_name(const char *raw_service_name);
