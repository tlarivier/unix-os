/*
 * strrchr.c — libc `strrchr`: last occurrence of byte `c` in `str`.
 *
 * Invariants:
 *  - Returns pointer to the last match before the terminating NUL.
 *  - If `c == '\0'`, returns the pointer to the terminating NUL.
 *  - Returns NULL if no match.
 *
 * Not allowed:
 *  - Treating `c` as multi-byte.
 *  - Reading past the terminating NUL.
 */

#include <stddef.h>

char *strrchr(const char *str, int c) {
  const char *last = NULL;
  while (*str) {
    if (*str == (char)c)
      last = str;
    str++;
  }
  return (c == '\0') ? (char *)str : (char *)last;
}
