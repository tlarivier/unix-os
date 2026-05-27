/*
 * strncat.c — libc `strncat`: append at most `n` bytes of `src` to `dest`,
 * always followed by a single NUL terminator (POSIX semantics).
 *
 * Invariants:
 *  - Returns `dest`.
 *  - Writes at most `n + 1` bytes after the original end of `dest`.
 *  - Final NUL is always written (unlike strncpy).
 *
 * Not allowed:
 *  - Operating on overlapping buffers.
 *  - Reading past src's NUL or past `n` source bytes, whichever first.
 */

#include <stddef.h>

char *strncat(char *dest, const char *src, size_t n) {
  char *d = dest;
  while (*d)
    d++;
  while (n-- && (*d++ = *src++))
    ;
  *d = '\0';
  return dest;
}
