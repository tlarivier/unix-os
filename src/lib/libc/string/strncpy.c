/*
 * strncpy.c — libc `strncpy`: copy at most `n` bytes; NUL-pad the rest.
 *
 * Invariants:
 *  - Writes EXACTLY `n` bytes total: src content up to first NUL, then NULs.
 *  - Never writes past `dest[n-1]` (the legacy bug wrote n+1 and corrupted
 *    Doom's lumpinfo[].wad_file — see feedback_libc_strncpy_overflow).
 *  - No NUL guarantee if strlen(src) >= n (POSIX semantics).
 *
 * Not allowed:
 *  - Writing more than `n` bytes under any circumstance.
 *  - Returning before completing the NUL-pad phase.
 */

#include <stddef.h>

char *strncpy(char *dest, const char *src, size_t n) {
  size_t i;
  for (i = 0; i < n && src[i] != '\0'; i++)
    dest[i] = src[i];
  for (; i < n; i++)
    dest[i] = '\0';
  return dest;
}
