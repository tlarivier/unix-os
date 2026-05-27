/*
 * strstr.c — libc `strstr`: naive O(n*m) substring search.
 *
 * Invariants:
 *  - Returns `haystack` if `needle` is empty (POSIX).
 *  - Returns NULL if not found, else pointer to the first match in haystack.
 *  - Pure function; no global state.
 *
 * Not allowed:
 *  - Reading past either string's terminating NUL.
 *  - Allocating any scratch buffer.
 */

#include <stddef.h>

char *strstr(const char *haystack, const char *needle) {
  if (!*needle)
    return (char *)haystack;
  for (; *haystack; haystack++) {
    const char *h = haystack;
    const char *n = needle;
    while (*h && *n && *h == *n) {
      h++;
      n++;
    }
    if (!*n)
      return (char *)haystack;
  }
  return NULL;
}
