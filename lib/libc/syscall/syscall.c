#include <stdint.h>

long _syscall(long num, long a1, long a2, long a3, long a4, long a5) {
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
        : "memory"
    );
    return ret;
}

long syscall0(long num) { return _syscall(num, 0, 0, 0, 0, 0); }
long syscall1(long num, long a1) { return _syscall(num, a1, 0, 0, 0, 0); }
long syscall2(long num, long a1, long a2) { return _syscall(num, a1, a2, 0, 0, 0); }
long syscall3(long num, long a1, long a2, long a3) { return _syscall(num, a1, a2, a3, 0, 0); }
long syscall4(long num, long a1, long a2, long a3, long a4) { return _syscall(num, a1, a2, a3, a4, 0); }
long syscall5(long num, long a1, long a2, long a3, long a4, long a5) { return _syscall(num, a1, a2, a3, a4, a5); }
