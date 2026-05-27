#ifndef KERNEL_KSTRING_H
#define KERNEL_KSTRING_H

#include <stddef.h>
#include <stdint.h>

size_t kstrlen(const char *s);
char *kstrcpy(char *dst, const char *src);
char *kstrncpy(char *dst, const char *src, size_t n);
int kstrcmp(const char *a, const char *b);
int kstrncmp(const char *a, const char *b, size_t n);
void *kmemset(void *ptr, int value, size_t num);
void *kmemcpy(void *dest, const void *src, size_t num);

#endif
