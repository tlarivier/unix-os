/*
 * strcat.c — libc `strcat`: append `src` (with its NUL) to the end of `dest`.
 *
 * Invariants:
 *  - Returns `dest`; caller must have sized it for strlen(dest)+strlen(src)+1.
 *  - Pure function; no global state.
 *
 * Not allowed:
 *  - Bounds checking (use strncat for that).
 *  - Operating on overlapping buffers.
 */

char *strcat(char *dest, const char *src) {
  char *d = dest;
  while (*d)
    d++;
  while ((*d++ = *src++))
    ;
  return dest;
}
