/*
 * strcmp.c — libc `strcmp`: lexicographic comparison of two NUL-terminated
 * strings.
 *
 * Invariants:
 *  - Returns the unsigned-byte difference at the first differing position, or
 * 0.
 *  - Stops at the first mismatch or the first NUL.
 *  - Pure function; no global state.
 *
 * Not allowed:
 *  - Treating bytes as signed (must cast through unsigned char).
 *  - Reading past either string's NUL terminator.
 */

int strcmp(const char *s1, const char *s2) {
  while (*s1 && *s1 == *s2) {
    s1++;
    s2++;
  }
  return *(unsigned char *)s1 - *(unsigned char *)s2;
}
