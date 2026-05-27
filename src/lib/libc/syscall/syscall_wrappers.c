/*
 * syscall_wrappers.c — `syscall_*` thin adapters (write/opendir/readdir/close)
 * routed to the corresponding POSIX wrappers; legacy from the initial port,
 * used only by userspace/bin/ls.c.
 *
 * Invariants:
 *  - Each function is a pure pass-through to a POSIX libc symbol declared
 *    extern at the top of the TU.
 *  - syscall_opendir() opens with O_RDONLY (no O_DIRECTORY enforcement yet).
 *
 * Not allowed:
 *  - Adding business logic — this file is a renaming layer.
 *  - Calling int $0x80 directly (always go through the POSIX wrapper).
 */

#include <../uapi/syscalls.h>
#include <lib/types.h>
#include <stddef.h>
#include <stdint.h>

extern int open(const char *path, int flags, int mode);
extern int close(int fd);
extern ssize_t write(int fd, const void *buffer, size_t count);
extern ssize_t getdents(int fd, void *dirp, size_t count);

ssize_t syscall_write(int fd, const void *buffer, size_t count) {
  return write(fd, buffer, count);
}

int syscall_opendir(const char *path) { return open(path, O_RDONLY, 0); }

ssize_t syscall_readdir(int fd, void *buffer, size_t count) {
  return getdents(fd, buffer, count);
}

int syscall_close(int fd) { return close(fd); }
