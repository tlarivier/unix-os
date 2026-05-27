#ifndef KERNEL_UACCESS_H
#define KERNEL_UACCESS_H

#include <stddef.h>
#include <stdint.h>

int copy_from_user(void *kernel_dst, const void *user_src, size_t n);
int copy_to_user(void *user_dst, const void *kernel_src, size_t n);
int copy_str_from_user(char *kdst, const char *usrc, size_t max);

int32_t validate_kernel_string(const char *str, size_t max_len);

#endif
