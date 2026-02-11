#include <stddef.h>
#include <stdint.h>

extern long _syscall(long num, long a1, long a2, long a3, long a4, long a5);

#define __NR_exit     1
#define __NR_fork     2
#define __NR_read     10
#define __NR_write    11
#define __NR_open     12
#define __NR_close    13
#define __NR_lseek    14
#define __NR_dup      15
#define __NR_dup2     16
#define __NR_pipe     17
#define __NR_execve   7
#define __NR_chdir    31
#define __NR_getcwd   32
#define __NR_mkdir    22
#define __NR_rmdir    23
#define __NR_unlink   24
#define __NR_link     28
#define __NR_symlink  29
#define __NR_readlink 99
#define __NR_chmod    26
#define __NR_chown    27
#define __NR_truncate 103
#define __NR_ftruncate 104
#define __NR_brk      40
#define __NR_getpid   4
#define __NR_getppid  5
#define __NR_getuid   71
#define __NR_getgid   72
#define __NR_geteuid  73
#define __NR_getegid  74
#define __NR_setuid   75
#define __NR_setgid   80
#define __NR_isatty   93
#define __NR_ioctl    54
#define __NR_fcntl    55
#define __NR_setpgid  84
#define __NR_getpgrp  86
#define __NR_setsid   87

typedef int32_t ssize_t;
typedef uint32_t off_t;
typedef int32_t pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;

ssize_t read(int fd, void *buf, size_t count) { return _syscall(__NR_read, fd, (long)buf, count, 0, 0); }
ssize_t write(int fd, const void *buf, size_t count) { return _syscall(__NR_write, fd, (long)buf, count, 0, 0); }
int open(const char *path, int flags, int mode) { return _syscall(__NR_open, (long)path, flags, mode, 0, 0); }
int close(int fd) { return _syscall(__NR_close, fd, 0, 0, 0, 0); }
off_t lseek(int fd, off_t offset, int whence) { return _syscall(__NR_lseek, fd, offset, whence, 0, 0); }
int dup(int oldfd) { return _syscall(__NR_dup, oldfd, 0, 0, 0, 0); }
int dup2(int oldfd, int newfd) { return _syscall(__NR_dup2, oldfd, newfd, 0, 0, 0); }
int pipe(int pipefd[2]) { return _syscall(__NR_pipe, (long)pipefd, 0, 0, 0, 0); }
int fcntl(int fd, int cmd, long arg) { return _syscall(__NR_fcntl, fd, cmd, arg, 0, 0); }
int ioctl(int fd, unsigned long request, long arg) { return _syscall(__NR_ioctl, fd, request, arg, 0, 0); }

int chdir(const char *path) { return _syscall(__NR_chdir, (long)path, 0, 0, 0, 0); }
int fchdir(int fd) { (void)fd; return -1; }
char *getcwd(char *buf, size_t size) { return _syscall(__NR_getcwd, (long)buf, size, 0, 0, 0) < 0 ? NULL : buf; }
int mkdir(const char *path, unsigned int mode) { return _syscall(__NR_mkdir, (long)path, mode, 0, 0, 0); }
int rmdir(const char *path) { return _syscall(__NR_rmdir, (long)path, 0, 0, 0, 0); }
int unlink(const char *path) { return _syscall(__NR_unlink, (long)path, 0, 0, 0, 0); }
int link(const char *oldpath, const char *newpath) { return _syscall(__NR_link, (long)oldpath, (long)newpath, 0, 0, 0); }
int symlink(const char *target, const char *linkpath) { return _syscall(__NR_symlink, (long)target, (long)linkpath, 0, 0, 0); }
ssize_t readlink(const char *path, char *buf, size_t bufsiz) { return _syscall(__NR_readlink, (long)path, (long)buf, bufsiz, 0, 0); }
int access(int fd, int mode) { (void)fd; (void)mode; return 0; }
int chown(const char *path, uid_t owner, gid_t group) { return _syscall(__NR_chown, (long)path, owner, group, 0, 0); }
int chmod(const char *path, unsigned int mode) { return _syscall(__NR_chmod, (long)path, mode, 0, 0, 0); }
int truncate(const char *path, off_t length) { return _syscall(__NR_truncate, (long)path, length, 0, 0, 0); }
int ftruncate(int fd, off_t length) { return _syscall(__NR_ftruncate, fd, length, 0, 0, 0); }

#define __NR_waitpid 3
#define __NR_getdents 30

int getdents(int fd, void *buf, size_t count) { return _syscall(__NR_getdents, fd, (long)buf, count, 0, 0); }

pid_t fork(void) { return _syscall(__NR_fork, 0, 0, 0, 0, 0); }
pid_t waitpid(pid_t pid, int *status, int options) { return _syscall(__NR_waitpid, pid, (long)status, options, 0, 0); }
pid_t wait(int *status) { return waitpid(-1, status, 0); }
pid_t getpid(void) { return _syscall(__NR_getpid, 0, 0, 0, 0, 0); }
pid_t getppid(void) { return _syscall(__NR_getppid, 0, 0, 0, 0, 0); }
pid_t getpgrp(void) { return _syscall(__NR_getpgrp, 0, 0, 0, 0, 0); }
pid_t setpgid(pid_t pid, pid_t pgid) { return _syscall(__NR_setpgid, pid, pgid, 0, 0, 0); }
pid_t setsid(void) { return _syscall(__NR_setsid, 0, 0, 0, 0, 0); }

int execve(const char *path, char *const argv[], char *const envp[]) { return _syscall(__NR_execve, (long)path, (long)argv, (long)envp, 0, 0); }
int execv(const char *path, char *const argv[]) { return execve(path, argv, NULL); }
int execvp(const char *file, char *const argv[]) { return execve(file, argv, NULL); }

uid_t getuid(void) { return _syscall(__NR_getuid, 0, 0, 0, 0, 0); }
uid_t geteuid(void) { return _syscall(__NR_geteuid, 0, 0, 0, 0, 0); }
gid_t getgid(void) { return _syscall(__NR_getgid, 0, 0, 0, 0, 0); }
gid_t getegid(void) { return _syscall(__NR_getegid, 0, 0, 0, 0, 0); }
int setuid(uid_t uid) { return _syscall(__NR_setuid, uid, 0, 0, 0, 0); }
int setgid(gid_t gid) { return _syscall(__NR_setgid, gid, 0, 0, 0, 0); }

int isatty(int fd) { return _syscall(__NR_isatty, fd, 0, 0, 0, 0); }
char *ttyname(int fd) { (void)fd; return "/dev/tty"; }

int brk(void *addr) { return _syscall(__NR_brk, (long)addr, 0, 0, 0, 0); }

static void *_brk_current = NULL;
void *sbrk(intptr_t increment) {
    if (!_brk_current) _brk_current = (void*)_syscall(__NR_brk, 0, 0, 0, 0, 0);
    if (increment == 0) return _brk_current;
    void *old = _brk_current;
    void *new_brk = (char*)_brk_current + increment;
    if (_syscall(__NR_brk, (long)new_brk, 0, 0, 0, 0) != (long)new_brk) return (void*)-1;
    _brk_current = new_brk;
    return old;
}

char *optarg = NULL;
int optind = 1, opterr = 1, optopt = 0;

int getopt(int argc, char *const argv[], const char *optstring) {
    static int optpos = 1;
    if (optind >= argc || !argv[optind] || argv[optind][0] != '-' || argv[optind][1] == '\0') return -1;
    if (argv[optind][1] == '-' && argv[optind][2] == '\0') { optind++; return -1; }
    
    char c = argv[optind][optpos];
    const char *p = optstring;
    while (*p && *p != c) p++;
    if (!*p) { optopt = c; return '?'; }
    
    if (p[1] == ':') {
        if (argv[optind][optpos + 1]) { optarg = &argv[optind][optpos + 1]; optind++; optpos = 1; }
        else if (optind + 1 < argc) { optarg = argv[++optind]; optind++; optpos = 1; }
        else { optopt = c; return '?'; }
    } else {
        if (!argv[optind][++optpos]) { optind++; optpos = 1; }
    }
    return c;
}

long sysconf(int name) { (void)name; return -1; }
int gethostname(char *name, size_t len) {
    const char *h = "unixos";
    size_t i = 0;
    while (i < len - 1 && h[i]) { name[i] = h[i]; i++; }
    name[i] = '\0';
    return 0;
}
