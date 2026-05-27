/*
 * memcmp.c — libc `memcmp`: byte-wise unsigned comparison of two buffers.
 *
 * Invariants:
 *  - Returns the signed difference of the first differing unsigned byte, or 0.
 *  - Stops at the first mismatch (does not read past it).
 *  - Pure function; no global state.
 *
 * Not allowed:
 *  - Returning early when num == 0 with a non-zero value (must return 0).
 *  - Treating bytes as signed.
 */

#include <stddef.h>

int memcmp(const void *ptr1, const void *ptr2, size_t num) {
  const unsigned char *p1 = (const unsigned char *)ptr1;
  const unsigned char *p2 = (const unsigned char *)ptr2;
  while (num--) {
    if (*p1 != *p2)
      return *p1 - *p2;
    p1++;
    p2++;
  }
  return 0;
}
