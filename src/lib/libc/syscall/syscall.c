/*
 * syscall.c — i386 INT 0x80 syscall trampolines used by the libc.
 *
 * Invariants:
 *  - `_syscall` is the raw trampoline: it returns EAX as-is, including
 *    negative -errno values (callers that want raw must use it).
 *  - `__syscall_with_errno` is the POSIX adapter: on EAX in [-4095, -1],
 *    sets `errno = -ret` and returns -1; otherwise returns the raw result.
 *  - Clobbers memory only; all argument registers are call-clobbered per
 *    the i386 Linux ABI (eax/ebx/ecx/edx/esi/edi).
 *
 * Not allowed:
 *  - Adding a 7th argument (the i386 int 0x80 ABI provides only 6 regs).
 *  - Touching errno from `_syscall` (POSIX adapter is the only writer).
 */

#include <stdint.h>

extern int errno;

long _syscall(long num, long a1, long a2, long a3, long a4, long a5) {
  long ret;
  __asm__ volatile("int $0x80"
                   : "=a"(ret)
                   : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
                   : "memory");
  return ret;
}

long __syscall_with_errno(long num, long a1, long a2, long a3, long a4,
                          long a5) {
  long ret = _syscall(num, a1, a2, a3, a4, a5);
  if (ret < 0 && ret > -4096) {
    errno = (int)-ret;
    return -1;
  }
  return ret;
}
