/*
 * uaccess.c — user<->kernel boundary helpers: copy_{from,to}_user,
 *             copy_str_from_user, validate_kernel_string.
 *
 * Invariants:
 *  - Every user pointer passes access_ok() (in [USER_MIN_VA, KERNEL_BASE)
 *    without integer wrap) AND user_range_resident() (all touched pages
 *    present in the current CR3) before any byte is copied.
 *  - copy_str_from_user stops at the first NUL and never reads past the
 *    caller-provided size, including across page boundaries.
 *  - The functions are called from syscall context only; preemption may
 *    be on, but no kernel lock is taken here.
 *
 * Not allowed:
 *  - Calling the VFS, the scheduler, or kmalloc.
 *  - Exposing access_ok / user_range_resident outside this TU (V09).
 */

#include <kernel/constants.h>
#include <kernel/errno.h>
#include <kernel/kstring.h>
#include <kernel/mm_internal.h>
#include <kernel/paging.h>
#include <kernel/uaccess.h>
#include <stdint.h>

#define USER_MIN_VA PAGE_SIZE_CONST

static int access_ok(const void *addr, size_t size) {
  if (!addr)
    return 0;
  uintptr_t a = (uintptr_t)addr;

  if (a >= KERNEL_BASE)
    return 0;
  if (a + size < a)
    return 0;
  if (a + size > KERNEL_BASE)
    return 0;
  if (a < USER_MIN_VA)
    return 0;

  return 1;
}

static int user_range_resident(uint32_t addr, size_t size) {
  if (size == 0)
    return 1;
  uint32_t start = addr & ~(uint32_t)PAGE_MASK;
  uint32_t end = (addr + size - 1) & ~(uint32_t)PAGE_MASK;
  for (uint32_t page = start;; page += PAGE_SIZE_CONST) {
    if (get_physical_addr_current(page) == 0)
      return 0;
    if (page == end)
      break;
  }
  return 1;
}

static int user_addr_resident(uint32_t addr) {
  return get_physical_addr_current(addr & ~(uint32_t)PAGE_MASK) != 0;
}

int copy_from_user(void *dst, const void *src, size_t n) {
  if (!dst || !src)
    return -EINVAL;
  if (n == 0)
    return 0;
  if (!access_ok(src, n))
    return -EFAULT;
  if (!user_range_resident((uint32_t)(uintptr_t)src, n))
    return -EFAULT;
  kmemcpy(dst, src, n);
  return 0;
}

int copy_to_user(void *dst, const void *src, size_t n) {
  if (!dst || !src)
    return -EINVAL;
  if (n == 0)
    return 0;
  if (!access_ok(dst, n))
    return -EFAULT;
  if (!user_range_resident((uint32_t)(uintptr_t)dst, n))
    return -EFAULT;
  kmemcpy(dst, src, n);
  return 0;
}

int copy_str_from_user(char *dst, const char *src, size_t max) {
  if (!dst || !src || max == 0)
    return -EINVAL;
  if (!access_ok(src, max))
    return -EFAULT;

  uintptr_t base = (uintptr_t)src;
  if (!user_addr_resident((uint32_t)base))
    return -EFAULT;
  uint32_t cur_page = ((uint32_t)base) & ~(uint32_t)PAGE_MASK;

  size_t i;
  for (i = 0; i < max - 1; i++) {
    uint32_t addr = (uint32_t)(base + i);
    uint32_t page = addr & ~(uint32_t)PAGE_MASK;
    if (page != cur_page) {
      if (!user_addr_resident(addr))
        return -EFAULT;
      cur_page = page;
    }
    char c = ((const char *)src)[i];
    dst[i] = c;
    if (c == '\0')
      return (int)i;
  }
  dst[max - 1] = '\0';
  return -EOVERFLOW;
}

int32_t validate_kernel_string(const char *str, size_t max_len) {
  if (!str)
    return -EINVAL;
  for (size_t i = 0; i < max_len; i++) {
    if (str[i] == '\0')
      return 0;
  }
  return -EOVERFLOW;
}
