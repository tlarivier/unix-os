/*
 * unistd_full.c — POSIX <unistd.h> wrappers for ~30 syscalls
 * (read/write/open/close/lseek/dup/pipe, fs ops, fork/exec/wait, brk/sbrk,
 * getopt).
 *
 * Invariants:
 *  - All POSIX-facing wrappers funnel through __syscall_with_errno: a kernel
 *    return in [-4095, -1] becomes `errno = -ret; return -1`.
 *  - brk/sbrk use raw _syscall (the kernel returns the new break, possibly
 *    a high address that looks negative) and own the file-static
 * `_brk_current`.
 *  - getopt globals (optarg/optind/opterr/optopt) live here as the single
 *    source of truth for userspace.
 *
 * Not allowed:
 *  - Hand-rolling errno translation per-wrapper (always route through SYS()).
 *  - Direct int $0x80 in this TU (use _syscall / __syscall_with_errno).
 */

#include <stddef.h>
#include <stdint.h>

extern long _syscall(long num, long a1, long a2, long a3, long a4, long a5);
extern long __syscall_with_errno(long num, long a1, long a2, long a3, long a4,
                                 long a5);
extern int errno;

#define SYS(n, a, b, c, d, e)                                                  \
  __syscall_with_errno((n), (long)(a), (long)(b), (long)(c), (long)(d),        \
                       (long)(e))

#define __NR_exit 1
#define __NR_fork 2
#define __NR_read 10
#define __NR_write 11
#define __NR_open 12
#define __NR_close 13
#define __NR_lseek 14
#define __NR_dup 15
#define __NR_dup2 16
#define __NR_pipe 17
#define __NR_execve 7
#define __NR_chdir 31
#define __NR_getcwd 32
#define __NR_mkdir 22
#define __NR_rmdir 23
#define __NR_unlink 24
#define __NR_link 28
#define __NR_chmod 26
#define __NR_chown 27
#define __NR_truncate 103
#define __NR_ftruncate 104
#define __NR_brk 40
#define __NR_getpid 4
#define __NR_getppid 5
#define __NR_getuid 71
#define __NR_getgid 72
#define __NR_geteuid 73
#define __NR_getegid 74
#define __NR_setuid 75
#define __NR_setgid 80
#define __NR_isatty 93
#define __NR_setpgid 84
#define __NR_getpgrp 86
#define __NR_setsid 87

typedef int32_t ssize_t;
typedef uint32_t off_t;
typedef int32_t pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;

ssize_t read(int fd, void *buf, size_t count) {
  return SYS(__NR_read, fd, buf, count, 0, 0);
}
ssize_t write(int fd, const void *buf, size_t count) {
  return SYS(__NR_write, fd, buf, count, 0, 0);
}
int open(const char *path, int flags, int mode) {
  return SYS(__NR_open, path, flags, mode, 0, 0);
}
int close(int fd) { return SYS(__NR_close, fd, 0, 0, 0, 0); }
off_t lseek(int fd, off_t offset, int whence) {
  return SYS(__NR_lseek, fd, offset, whence, 0, 0);
}
int dup(int oldfd) { return SYS(__NR_dup, oldfd, 0, 0, 0, 0); }
int dup2(int oldfd, int newfd) { return SYS(__NR_dup2, oldfd, newfd, 0, 0, 0); }
int pipe(int pipefd[2]) { return SYS(__NR_pipe, pipefd, 0, 0, 0, 0); }

int chdir(const char *path) { return SYS(__NR_chdir, path, 0, 0, 0, 0); }
int fchdir(int fd) {
  (void)fd;
  errno = 38 /*ENOSYS*/;
  return -1;
}
char *getcwd(char *buf, size_t size) {
  long r = SYS(__NR_getcwd, buf, size, 0, 0, 0);
  return (r < 0) ? NULL : buf;
}
int mkdir(const char *path, unsigned int mode) {
  return SYS(__NR_mkdir, path, mode, 0, 0, 0);
}
int rmdir(const char *path) { return SYS(__NR_rmdir, path, 0, 0, 0, 0); }
int unlink(const char *path) { return SYS(__NR_unlink, path, 0, 0, 0, 0); }
int link(const char *oldpath, const char *newpath) {
  return SYS(__NR_link, oldpath, newpath, 0, 0, 0);
}
int access(int fd, int mode) {
  (void)fd;
  (void)mode;
  return 0;
}
int chown(const char *path, uid_t owner, gid_t group) {
  return SYS(__NR_chown, path, owner, group, 0, 0);
}
int chmod(const char *path, unsigned int mode) {
  return SYS(__NR_chmod, path, mode, 0, 0, 0);
}
int truncate(const char *path, off_t length) {
  return SYS(__NR_truncate, path, length, 0, 0, 0);
}
int ftruncate(int fd, off_t length) {
  return SYS(__NR_ftruncate, fd, length, 0, 0, 0);
}

#define __NR_waitpid 3
#define __NR_getdents 30

int getdents(int fd, void *buf, size_t count) {
  return SYS(__NR_getdents, fd, buf, count, 0, 0);
}

pid_t fork(void) { return SYS(__NR_fork, 0, 0, 0, 0, 0); }
pid_t waitpid(pid_t pid, int *status, int options) {
  return SYS(__NR_waitpid, pid, status, options, 0, 0);
}
pid_t wait(int *status) { return waitpid(-1, status, 0); }

pid_t getpid(void) { return SYS(__NR_getpid, 0, 0, 0, 0, 0); }
pid_t getppid(void) { return SYS(__NR_getppid, 0, 0, 0, 0, 0); }
pid_t getpgrp(void) { return SYS(__NR_getpgrp, 0, 0, 0, 0, 0); }
pid_t setpgid(pid_t pid, pid_t pgid) {
  return SYS(__NR_setpgid, pid, pgid, 0, 0, 0);
}
pid_t setsid(void) { return SYS(__NR_setsid, 0, 0, 0, 0, 0); }

int execve(const char *path, char *const argv[], char *const envp[]) {
  return SYS(__NR_execve, path, argv, envp, 0, 0);
}
int execv(const char *path, char *const argv[]) {
  return execve(path, argv, NULL);
}
int execvp(const char *file, char *const argv[]) {
  return execve(file, argv, NULL);
}

uid_t getuid(void) { return SYS(__NR_getuid, 0, 0, 0, 0, 0); }
uid_t geteuid(void) { return SYS(__NR_geteuid, 0, 0, 0, 0, 0); }
gid_t getgid(void) { return SYS(__NR_getgid, 0, 0, 0, 0, 0); }
gid_t getegid(void) { return SYS(__NR_getegid, 0, 0, 0, 0, 0); }
int setuid(uid_t uid) { return SYS(__NR_setuid, uid, 0, 0, 0, 0); }
int setgid(gid_t gid) { return SYS(__NR_setgid, gid, 0, 0, 0, 0); }

int isatty(int fd) { return SYS(__NR_isatty, fd, 0, 0, 0, 0); }
char *ttyname(int fd) {
  (void)fd;
  return "/dev/tty";
}

int brk(void *addr) {
  long r = _syscall(__NR_brk, (long)addr, 0, 0, 0, 0);
  if (r < 0 && r > -4096) {
    errno = (int)-r;
    return -1;
  }
  return 0;
}

static void *_brk_current = NULL;
void *sbrk(intptr_t increment) {
  if (!_brk_current)
    _brk_current = (void *)_syscall(__NR_brk, 0, 0, 0, 0, 0);
  if (increment == 0)
    return _brk_current;
  void *old = _brk_current;
  void *new_brk = (char *)_brk_current + increment;
  if (_syscall(__NR_brk, (long)new_brk, 0, 0, 0, 0) != (long)new_brk) {
    errno = 12 /*ENOMEM*/;
    return (void *)-1;
  }
  _brk_current = new_brk;
  return old;
}

char *optarg = NULL;
int optind = 1, opterr = 1, optopt = 0;

int getopt(int argc, char *const argv[], const char *optstring) {
  static int optpos = 1;
  if (optind >= argc || !argv[optind] || argv[optind][0] != '-' ||
      argv[optind][1] == '\0')
    return -1;
  if (argv[optind][1] == '-' && argv[optind][2] == '\0') {
    optind++;
    return -1;
  }

  char c = argv[optind][optpos];
  const char *p = optstring;
  while (*p && *p != c)
    p++;
  if (!*p) {
    optopt = c;
    return '?';
  }

  if (p[1] == ':') {
    if (argv[optind][optpos + 1]) {
      optarg = &argv[optind][optpos + 1];
      optind++;
      optpos = 1;
    } else if (optind + 1 < argc) {
      optarg = argv[++optind];
      optind++;
      optpos = 1;
    } else {
      optopt = c;
      return '?';
    }
  } else {
    if (!argv[optind][++optpos]) {
      optind++;
      optpos = 1;
    }
  }
  return c;
}

long sysconf(int name) {
  (void)name;
  return -1;
}
int gethostname(char *name, size_t len) {
  const char *h = "unixos";
  size_t i = 0;
  while (i < len - 1 && h[i]) {
    name[i] = h[i];
    i++;
  }
  name[i] = '\0';
  return 0;
}
