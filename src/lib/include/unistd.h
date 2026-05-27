#ifndef _LIBC_UNISTD_H
#define _LIBC_UNISTD_H

#include <sys/types.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int open(const char *path, int flags, ...);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int pipe(int pipefd[2]);

int chdir(const char *path);
int fchdir(int fd);
char *getcwd(char *buf, size_t size);
int mkdir(const char *path, unsigned int mode);
int rmdir(const char *path);
int unlink(const char *path);
int link(const char *oldpath, const char *newpath);
int access(int fd, int mode);
int chown(const char *path, uid_t owner, gid_t group);
int chmod(const char *path, unsigned int mode);
int truncate(const char *path, off_t length);
int ftruncate(int fd, off_t length);

pid_t fork(void);
pid_t getpid(void);
pid_t getppid(void);
pid_t waitpid(pid_t pid, int *status, int options);
pid_t wait(int *status);
pid_t getpgrp(void);
pid_t setpgid(pid_t pid, pid_t pgid);
pid_t setsid(void);
int execve(const char *path, char *const argv[], char *const envp[]);
int execv(const char *path, char *const argv[]);
int execvp(const char *file, char *const argv[]);
void _exit(int status);

uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);

int isatty(int fd);
char *ttyname(int fd);

int brk(void *addr);
void *sbrk(intptr_t increment);

unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);

int getopt(int argc, char *const argv[], const char *optstring);
extern char *optarg;
extern int optind, opterr, optopt;

long sysconf(int name);
int gethostname(char *name, size_t len);

#endif
