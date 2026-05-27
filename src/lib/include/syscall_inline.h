#ifndef _SYSCALL_INLINE_H
#define _SYSCALL_INLINE_H

#include <stdint.h>

static inline long __syscall0(int num) {
  long ret;
  __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num) : "memory");
  return ret;
}

static inline long __syscall1(int num, long a1) {
  long ret;
  __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1) : "memory");
  return ret;
}

static inline long __syscall2(int num, long a1, long a2) {
  long ret;
  __asm__ volatile("int $0x80"
                   : "=a"(ret)
                   : "a"(num), "b"(a1), "c"(a2)
                   : "memory");
  return ret;
}

static inline long __syscall3(int num, long a1, long a2, long a3) {
  long ret;
  __asm__ volatile("int $0x80"
                   : "=a"(ret)
                   : "a"(num), "b"(a1), "c"(a2), "d"(a3)
                   : "memory");
  return ret;
}

static inline long __syscall4(int num, long a1, long a2, long a3, long a4) {
  long ret;
  __asm__ volatile("int $0x80"
                   : "=a"(ret)
                   : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4)
                   : "memory");
  return ret;
}

static inline long __syscall5(int num, long a1, long a2, long a3, long a4,
                              long a5) {
  long ret;
  __asm__ volatile("int $0x80"
                   : "=a"(ret)
                   : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
                   : "memory");
  return ret;
}

#endif
