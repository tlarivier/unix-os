#ifndef _LIBC_STRINGS_H
#define _LIBC_STRINGS_H

#include <stddef.h>
#include <string.h>

#define bcopy(src, dst, n) memmove((dst), (src), (n))
#define bzero(s, n) memset((s), 0, (n))
#define bcmp(s1, s2, n) memcmp((s1), (s2), (n))

int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);

#endif
