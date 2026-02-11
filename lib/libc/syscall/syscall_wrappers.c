#include <stdint.h>
#include <stddef.h>
#include <lib/types.h>
#include <../uapi/syscalls.h>

extern int open(const char* path, int flags, int mode);
extern int close(int fd);
extern ssize_t write(int fd, const void* buffer, size_t count);
extern ssize_t getdents(int fd, void* dirp, size_t count);

ssize_t syscall_write(int fd, const void* buffer, size_t count) {
    return write(fd, buffer, count);
}

int syscall_opendir(const char* path) {
    return open(path, O_RDONLY, 0);
}

ssize_t syscall_readdir(int fd, void* buffer, size_t count) {
    return getdents(fd, buffer, count);
}

int syscall_close(int fd) {
    return close(fd);
}
