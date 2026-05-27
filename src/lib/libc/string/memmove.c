/*
 * memmove.c — libc `memmove`: overlap-safe byte copy.
 *
 * Invariants:
 *  - Copies forward if `dest < src`, backward otherwise; correct for any
 * overlap.
 *  - Copies exactly `num` bytes and returns `dest`.
 *  - Pure function; no global state.
 *
 * Not allowed:
 *  - Assuming non-overlap (callers may rely on the overlap guarantee).
 *  - Calling other libc helpers.
 */

#include <stddef.h>

void *memmove(void *dest, const void *src, size_t num) {
  unsigned char *d = (unsigned char *)dest;
  const unsigned char *s = (const unsigned char *)src;
  if (d < s) {
    while (num--)
      *d++ = *s++;
  } else {
    d += num;
    s += num;
    while (num--)
      *--d = *--s;
  }
  return dest;
}
