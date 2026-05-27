/*
 * kstring.c — kernel string and memory primitives (kstr_*, kmem_*) plus
 *             libgcc-implicit memset/memcpy aliases.
 *
 * Invariants:
 *  - No dependencies beyond <stdint.h>/<stddef.h>; callable at any moment
 *    including early-boot before percpu/gdt are wired.
 *  - NULL-safe on src/dst pointers (early return, never panic).
 *  - kstrncpy always NUL-terminates for n >= 1 (non-POSIX; truncates
 *    silently when strlen(src) >= n-1).
 *
 * Not allowed:
 *  - Allocating memory, taking a lock, calling kprintf.
 *  - Dereferencing a __user pointer (that is copy_from_user's job).
 *  - Recursing.
 */

#include <kernel/kstring.h>

size_t kstrlen(const char *s) {
  if (!s)
    return 0;
  size_t n = 0;
  while (s[n])
    n++;
  return n;
}

char *kstrcpy(char *dst, const char *src) {
  if (!dst || !src)
    return dst;
  char *d = dst;
  while ((*d++ = *src++)) {
  }
  return dst;
}

char *kstrncpy(char *dst, const char *src, size_t n) {
  if (!dst || !src || n == 0)
    return dst;
  size_t i;
  for (i = 0; i < n - 1 && src[i]; i++) {
    dst[i] = src[i];
  }
  dst[i] = '\0';
  return dst;
}

int kstrcmp(const char *a, const char *b) {
  if (a == b)
    return 0;
  if (!a)
    return -1;
  if (!b)
    return 1;
  while (*a && (*a == *b)) {
    a++;
    b++;
  }
  return (int)(unsigned char)(*a) - (int)(unsigned char)(*b);
}

int kstrncmp(const char *a, const char *b, size_t n) {
  if (!a || !b)
    return a ? 1 : (b ? -1 : 0);
  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i]) {
      return (unsigned char)a[i] - (unsigned char)b[i];
    }
    if (a[i] == '\0')
      break;
  }
  return 0;
}

void *kmemset(void *ptr, int value, size_t num) {
  unsigned char *p = (unsigned char *)ptr;
  unsigned char c = (unsigned char)value;

  for (size_t i = 0; i < num; i++) {
    p[i] = c;
  }
  return ptr;
}

void *kmemcpy(void *dest, const void *src, size_t num) {
  unsigned char *d = (unsigned char *)dest;
  const unsigned char *s = (const unsigned char *)src;

  for (size_t i = 0; i < num; i++) {
    d[i] = s[i];
  }
  return dest;
}

void *memset(void *ptr, int value, size_t num) {
  return kmemset(ptr, value, num);
}

void *memcpy(void *dest, const void *src, size_t num) {
  return kmemcpy(dest, src, num);
}
