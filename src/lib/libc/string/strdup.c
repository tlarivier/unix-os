/*
 * strdup.c — libc `strdup`: malloc + copy of a NUL-terminated string.
 *
 * Invariants:
 *  - Returns a freshly malloc'd copy of `str` (caller takes ownership) or NULL.
 *  - Result is NUL-terminated and exactly strlen(str)+1 bytes long.
 *  - The only string TU that depends on the allocator.
 *
 * Not allowed:
 *  - Returning the input pointer (caller-owned malloc result required).
 *  - Operating on a non-NUL-terminated buffer.
 */

#include <stddef.h>

extern size_t strlen(const char *str);
extern void *malloc(size_t size);
extern void *memcpy(void *dest, const void *src, size_t num);

char *strdup(const char *str) {
  size_t len = strlen(str) + 1;
  char *dup = (char *)malloc(len);
  if (dup)
    memcpy(dup, str, len);
  return dup;
}
