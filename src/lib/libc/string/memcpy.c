/*
 * memcpy.c — libc `memcpy`: byte-wise non-overlap-safe copy.
 *
 * Invariants:
 *  - Copies exactly `num` bytes from `src` to `dest` and returns `dest`.
 *  - Caller must guarantee buffers do not overlap (POSIX). Use memmove
 * otherwise.
 *  - Pure function; no global state.
 *
 * Not allowed:
 *  - Overlap-safe semantics (that contract belongs to memmove).
 *  - Calling other libc helpers.
 */

#include <stddef.h>

void *memcpy(void *dest, const void *src, size_t num) {
  unsigned char *d = (unsigned char *)dest;
  const unsigned char *s = (const unsigned char *)src;
  while (num--)
    *d++ = *s++;
  return dest;
}
