/*
 * strlen.c — libc `strlen`: length of a NUL-terminated C string.
 *
 * Invariants:
 *  - Returns the number of bytes before the terminating NUL (excluded).
 *  - Pure function; no global state.
 *
 * Not allowed:
 *  - Operating on non-NUL-terminated buffers (UB by contract).
 *  - Calling other libc helpers.
 */

#include <stddef.h>

size_t strlen(const char *str) {
  const char *s = str;
  while (*s)
    s++;
  return s - str;
}
