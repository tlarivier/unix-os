/*
 * strchr.c — libc `strchr`: first occurrence of byte `c` in `str`.
 *
 * Invariants:
 *  - Returns a pointer to the first match; if `c == '\0'`, returns the
 *    pointer to the terminating NUL (POSIX requirement).
 *  - Returns NULL if no match before the terminating NUL.
 *  - Pure function; no global state.
 *
 * Not allowed:
 *  - Treating `c` as a multi-byte value (`(char)c` only).
 *  - Reading past the terminating NUL.
 */

#include <stddef.h>

char *strchr(const char *str, int c) {
  while (*str) {
    if (*str == (char)c)
      return (char *)str;
    str++;
  }
  return (c == '\0') ? (char *)str : NULL;
}
