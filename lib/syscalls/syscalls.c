#include <stdint.h>
#include <stddef.h>
#include <kernel/types.h>
#include <../uapi/syscalls.h>
#include <../uapi/types.h>
#include <../uapi/signal.h>
#include <../uapi/errno.h>
#include <stddef.h>

#define IS_ERROR(x)     ((x) < 0)

struct timespec {
    time_t tv_sec;        
    long   tv_nsec;       
};

struct __kernel_dirent {
    unsigned long d_ino;
    unsigned long d_off;
    unsigned short d_reclen;
    char d_name[256];
};

/*
 * Syscall dispatcher
 * 
 * Uses standard Linux x86-32 syscall convention:
 * - EAX: syscall number
 * - EBX, ECX, EDX, ESI, EDI, EBP: arguments 1-6
 * - INT 0x80: invoke kernel
 * - Return: EAX contains result (positive) or -errno (negative)
 */
static inline long _syscall0(long number)
{
    long ret;
    __asm__ volatile("int $0x80"
                : "=a" (ret)
                : "a" (number)
                : "memory");
    return ret;
}

static inline long _syscall1(long number, long arg1)
{
    long ret;
    __asm__ volatile("int $0x80"
                : "=a" (ret)
                : "a" (number), "b" (arg1)
                : "memory");
    return ret;
}

static inline long _syscall2(long number, long arg1, long arg2)
{
    long ret;
    __asm__ volatile("int $0x80"
                : "=a" (ret)
                : "a" (number), "b" (arg1), "c" (arg2)
                : "memory");
    return ret;
}

static inline long _syscall3(long number, long arg1, long arg2, long arg3)
{
    long ret;
    __asm__ volatile("int $0x80"
                : "=a" (ret)
                : "a" (number), "b" (arg1), "c" (arg2), "d" (arg3)
                : "memory");
    return ret;
}

static inline long _syscall4(long number, long arg1, long arg2, long arg3, long arg4)
{
    long ret;
    __asm__ volatile("int $0x80"
                : "=a" (ret)
                : "a" (number), "b" (arg1), "c" (arg2), "d" (arg3), "S" (arg4)
                : "memory");
    return ret;
}

static inline long _syscall5(long number, long arg1, long arg2, long arg3, long arg4, long arg5)
{
    long ret;
    __asm__ volatile("int $0x80"
                : "=a" (ret)
                : "a" (number), "b" (arg1), "c" (arg2), "d" (arg3), "S" (arg4), "D" (arg5)
                : "memory");
    return ret;
}

void _exit(int status)
{
    _syscall1(__NR_exit, status);
    __builtin_unreachable(); /* Never returns */
}

__kernel_pid_t getpid(void)
{
    return _syscall0(__NR_getpid);
}

__kernel_pid_t getppid(void)
{
    return _syscall0(__NR_getppid);
}

__kernel_pid_t fork(void)
{
    return _syscall0(__NR_fork);
}

int execve(const char *pathname, char *const argv[], char *const envp[])
{
    return _syscall3(__NR_execve, (long)pathname, (long)argv, (long)envp);
}

__kernel_pid_t wait4(__kernel_pid_t pid, int *wstatus, int options, void *rusage)
{
    return _syscall4(__NR_wait4, pid, (long)wstatus, options, (long)rusage);
}

int open(const char *pathname, int flags, mode_t mode)
{
    return _syscall3(__NR_open, (long)pathname, flags, mode);
}

int close(int fd)
{
    return _syscall1(__NR_close, fd);
}

ssize_t read(int fd, void *buf, size_t count)
{
    return _syscall3(__NR_read, fd, (long)buf, count);
}

ssize_t write(int fd, const void *buf, size_t count)
{
    return _syscall3(__NR_write, fd, (long)buf, count);
}

off_t lseek(int fd, off_t offset, int whence)
{
    return _syscall3(__NR_lseek, fd, offset, whence);
}

int dup(int oldfd)
{
    return _syscall1(__NR_dup, oldfd);
}

int dup2(int oldfd, int newfd)
{
    return _syscall2(__NR_dup2, oldfd, newfd);
}

int pipe(int pipefd[2])
{
    return _syscall1(__NR_pipe, (long)pipefd);
}

int stat(const char *pathname, struct __kernel_stat *statbuf)
{
    return _syscall2(__NR_stat, (long)pathname, (long)statbuf);
}

int fstat(int fd, struct __kernel_stat *statbuf)
{
    return _syscall2(__NR_fstat, fd, (long)statbuf);
}

int mkdir(const char *pathname, mode_t mode)
{
    return _syscall2(__NR_mkdir, (long)pathname, mode);
}

int rmdir(const char *pathname)
{
    return _syscall1(__NR_rmdir, (long)pathname);
}

int unlink(const char *pathname)
{
    return _syscall1(__NR_unlink, (long)pathname);
}

int rename(const char *oldpath, const char *newpath)
{
    return _syscall2(__NR_rename, (long)oldpath, (long)newpath);
}

int chmod(const char *pathname, mode_t mode)
{
    return _syscall2(__NR_chmod, (long)pathname, mode);
}

int chown(const char *pathname, __kernel_uid_t owner, __kernel_gid_t group)
{
    return _syscall3(__NR_chown, (long)pathname, owner, group);
}

int getdents(int fd, struct __kernel_dirent *dirp, unsigned int count)
{
    return _syscall3(__NR_getdents, fd, (long)dirp, count);
}

int chdir(const char *path)
{
    return _syscall1(__NR_chdir, (long)path);
}

char *getcwd(char *buf, size_t size)
{
    long ret = _syscall2(__NR_getcwd, (long)buf, size);
    return (ret < 0) ? (char *)0 : buf;
}

/* Memory management */
void *brk(void *addr)
{
    long ret = _syscall1(__NR_brk, (long)addr);
    if (IS_ERROR(ret)) {
        return (void*)-1;  /* Error */
    }
    return (void*)ret;     /* New break address */
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    (void)offset;  /* Unused in simple implementation */
    long ret = _syscall5(__NR_mmap, (long)addr, length, prot, flags, fd);
    return (void *)ret;
}

int munmap(void *addr, size_t length)
{
    return _syscall2(__NR_munmap, (long)addr, length);
}

int mprotect(void *addr, size_t len, int prot)
{
    return _syscall3(__NR_mprotect, (long)addr, len, prot);
}

/* Signal handling */
long signal(int signum, long handler)
{
    return _syscall2(__NR_signal, signum, handler);
}

int kill(__kernel_pid_t pid, int sig)
{
    return _syscall2(__NR_kill, pid, sig);
}

__kernel_time_t time(__kernel_time_t *tloc)
{
    return _syscall1(__NR_time, (long)tloc);
}

int nanosleep(const void *req, void *rem)
{
    return _syscall2(__NR_nanosleep, (long)req, (long)rem);
}

int uname(void *buf)
{
    return _syscall1(__NR_uname, (long)buf);
}

__kernel_uid_t getuid(void)
{
    return _syscall0(__NR_getuid);
}

__kernel_gid_t getgid(void)
{
    return _syscall0(__NR_getgid);
}

int reboot(int magic, int magic2, int cmd, void *arg)
{
    return _syscall4(__NR_reboot, magic, magic2, cmd, (long)arg);
}

int mount(const char *source, const char *target, const char *filesystemtype,
          unsigned long mountflags, const void *data)
{
    return _syscall5(__NR_mount, (long)source, (long)target, (long)filesystemtype,
                    mountflags, (long)data);
}

int umount(const char *target)
{
    return _syscall1(__NR_umount, (long)target);
}

/* Development/debugging syscalls */
int debug_print(const char *msg)
{
    return _syscall1(__NR_debug, (long)msg);
}

int procinfo(void *buf, size_t bufsize)
{
    return _syscall2(__NR_procinfo, (long)buf, bufsize);
}

/*
 * Legacy compatibility wrappers - DEPRECATED
 */

/* FIXED: void syscall_exit() - never returns */
__attribute__((noreturn)) void syscall_exit(int status) {
    _exit(status);
    __builtin_unreachable();
}

/* CORRECT: ssize_t for byte count returns */
ssize_t syscall_read(int fd, char *buffer, size_t count) {
    return read(fd, buffer, count);
}

ssize_t syscall_write(int fd, const char *buffer, size_t count) {
    return write(fd, buffer, count);
}

/* CORRECT: int for file descriptor returns */
int syscall_open(const char *path, int flags) {
    return open(path, flags, 0644);
}

int syscall_close(int fd) {
    return close(fd);
}

/* FIXED: pid_t syscall_getpid() - returns actual PID */
pid_t syscall_getpid(void) {
    return getpid();
}

/* CORRECT: int for success/error returns */
int syscall_mkdir(const char *path) {
    return mkdir(path, 0755);
}

/* FIXED: void* syscall_brk() - returns new break address */
void* syscall_brk(void *addr) {
    return brk(addr);
}

/* ADDED: Missing syscall functions with correct types */
pid_t syscall_waitpid(pid_t pid, int *status, int options) {
    return wait4(pid, status, options, NULL);
}

int syscall_reboot(void) {
    return reboot(0x1234, 0x5678, 0x4321FEDC, NULL);
}

int syscall_sleep(uint32_t ms) {
    /* Convert milliseconds to nanosleep */
    struct timespec req = { ms / 1000, (ms % 1000) * 1000000L };
    return nanosleep(&req, NULL);
}

void syscall_yield(void) {
    /* yield doesn't exist in our syscalls, use nanosleep(0) */
    struct timespec req = { 0, 1 };
    nanosleep(&req, NULL);
}

int syscall_opendir(const char* path) {
    return open(path, O_RDONLY, 0);
}

ssize_t syscall_readdir(int fd, char* buffer, size_t bufsize) {
    return getdents(fd, (struct __kernel_dirent*)buffer, bufsize);
}

int syscall_closedir(int fd) {
    return close(fd);
}

ssize_t syscall_getproclist(char* buffer, size_t size) {
    return procinfo(buffer, size);
}
