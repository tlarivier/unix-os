/*
 * strcpy.c — libc `strcpy`: copy `src` (including the terminating NUL) into
 * `dest`.
 *
 * Invariants:
 *  - Returns `dest`.
 *  - Caller owns `dest` and must size it for strlen(src)+1 bytes.
 *  - Pure function; no global state.
 *
 * Not allowed:
 *  - Bounds checking (use strncpy for that).
 *  - Calling other libc helpers.
 */

char *strcpy(char *dest, const char *src) {
  char *d = dest;
  while ((*d++ = *src++))
    ;
  return dest;
}
