#ifndef KERNEL_UACCESS_H
#define KERNEL_UACCESS_H

#include <stdint.h>
#include <stddef.h>

int access_ok(const void* addr, size_t size);
int copy_from_user(void* kernel_dst, const void* user_src, size_t n);
int copy_to_user(void* user_dst, const void* kernel_src, size_t n);
int copy_str_from_user(char* kdst, const char* usrc, size_t max);

#define IS_ERROR(x) ((x) < 0)

#endif
