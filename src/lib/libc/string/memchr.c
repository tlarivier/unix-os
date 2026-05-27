/*
 * memchr.c — libc `memchr`: linear search for the first byte equal to `value`.
 *
 * Invariants:
 *  - Returns a pointer to the first match in [ptr, ptr+num), or NULL.
 *  - Compares as unsigned char.
 *  - Pure function; no global state.
 *
 * Not allowed:
 *  - Reading past the first match.
 *  - Treating `value` as signed.
 */

#include <stddef.h>

void *memchr(const void *ptr, int value, size_t num) {
  const unsigned char *p = (const unsigned char *)ptr;
  while (num--) {
    if (*p == (unsigned char)value)
      return (void *)p;
    p++;
  }
  return NULL;
}
