#ifndef _RTLD_SYSCALL_H
#define _RTLD_SYSCALL_H

#include <stdint.h>
#include <stddef.h>

#define __NR_exit     1
#define __NR_read    10
#define __NR_write   11
#define __NR_open    12
#define __NR_close   13
#define __NR_lseek   14
#define __NR_mmap    41
#define __NR_munmap  42

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20

static inline int rtld_open(const char *path, int flags, int mode) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(__NR_open), "b"(path), "c"(flags), "d"(mode));
    return ret;
}

static inline int rtld_read(int fd, void *buf, size_t count) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(__NR_read), "b"(fd), "c"(buf), "d"(count));
    return ret;
}

static inline int rtld_write(int fd, const void *buf, size_t count) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(__NR_write), "b"(fd), "c"(buf), "d"(count));
    return ret;
}

static inline int rtld_close(int fd) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(__NR_close), "b"(fd));
    return ret;
}

static inline int rtld_lseek(int fd, int off, int whence) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(__NR_lseek), "b"(fd), "c"(off), "d"(whence));
    return ret;
}

static inline void *rtld_mmap(void *addr, size_t len, int prot, int flags, int fd, int off) {
    void *ret;
    (void)off;  /* offset not used for MAP_ANONYMOUS */
    __asm__ volatile("int $0x80" 
        : "=a"(ret) 
        : "a"(__NR_mmap), "b"(addr), "c"(len), "d"(prot), "S"(flags), "D"(fd));
    return ret;
}

static inline int rtld_munmap(void *addr, size_t len) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(__NR_munmap), "b"(addr), "c"(len));
    return ret;
}

static inline void rtld_exit(int code) {
    __asm__ volatile("int $0x80" : : "a"(__NR_exit), "b"(code));
    __builtin_unreachable();
}

#endif /* _RTLD_SYSCALL_H */
