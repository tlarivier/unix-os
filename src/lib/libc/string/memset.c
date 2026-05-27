/*
 * memset.c — libc `memset`: fill `num` bytes of `ptr` with `(unsigned
 * char)value`.
 *
 * Invariants:
 *  - Writes exactly `num` bytes and returns `ptr`.
 *  - Naive byte loop (no 4/8-byte unrolling).
 *  - Pure function; no global state.
 *
 * Not allowed:
 *  - Skipping writes for value == 0 (caller may rely on every byte touched).
 *  - Calling other libc helpers.
 */

#include <stddef.h>

void *memset(void *ptr, int value, size_t num) {
  unsigned char *p = (unsigned char *)ptr;
  while (num--)
    *p++ = (unsigned char)value;
  return ptr;
}
