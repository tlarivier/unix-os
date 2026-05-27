/*
 * strncmp.c — libc `strncmp`: bounded lexicographic comparison.
 *
 * Invariants:
 *  - Compares at most `n` bytes; returns 0 once `n` is exhausted with no diff.
 *  - Treats bytes as unsigned for the returned difference.
 *  - Pure function; no global state.
 *
 * Not allowed:
 *  - Reading past `n` bytes from either string.
 *  - Treating bytes as signed.
 */

#include <stddef.h>

int strncmp(const char *s1, const char *s2, size_t n) {
  while (n && *s1 && *s1 == *s2) {
    s1++;
    s2++;
    n--;
  }
  return n ? (*(unsigned char *)s1 - *(unsigned char *)s2) : 0;
}
