# UnixOS Syscall Reference

The kernel exposes 65 syscalls through INT 0x80, plus a small set of
in-kernel driver interfaces consumed by `src/kernel/init/main.c`. This
document covers the syscall surface only; per-file kernel contracts
live as block-comment headers in each `.c`.

## ABI summary

```
INT 0x80
EAX  = syscall number (uapi/syscalls.h __NR_*)
EBX  = arg0   ECX = arg1   EDX = arg2
ESI  = arg3   EDI = arg4
EAX  = return value (negative = -errno)
```

- Argument count: the dispatch table always passes five `uint32_t`s.
  Unused slots are filled with garbage that the handler ignores.
- Return type: `int32_t`. Negative values are POSIX errnos from
  `uapi/errno.h`. `IS_ERROR(x)` checks the high bit.
- User pointers: every pointer argument is opaque to the dispatcher.
  Handlers validate with `copy_from_user` / `copy_to_user` from
  `src/kernel/mm/uaccess.c`. Direct pointer dereference inside a syscall
  is a bug.
- Canary: `process_check_current_canary()` runs at syscall prologue
  AND epilogue. A mismatch panics with a stack-smashing diagnostic.

## Process & identity

| nr  | name           | signature                                | location           |
|-----|----------------|------------------------------------------|--------------------|
|   1 | exit           | `void(int status)`                       | sys_proc.c         |
|   2 | fork           | `pid_t(void)` (via sys_fork_wrap)        | sys_proc.c, fork.c |
|   3 | waitpid        | `pid_t(pid_t, int *status, int opts)`    | sys_proc.c         |
|   4 | getpid         | `pid_t(void)`                            | sys_proc.c         |
|   5 | getppid        | `pid_t(void)`                            | sys_proc.c         |
|   7 | execve         | `int(const char *, char *const[], char *const[])` | sys_misc.c |
|  50 | signal         | `sighandler_t(int sig, sighandler_t)`    | sys_proc.c         |
|  51 | kill           | `int(pid_t, int sig)`                    | sys_proc.c         |
| 222 | set_tid_address| `pid_t(int *)`  (stub)                   | sys_proc.c         |
| 223 | gettid         | `pid_t(void)`  (= pid in single-thread)  | sys_proc.c         |

## Credentials

| nr  | name      | signature                | location    |
|-----|-----------|--------------------------|-------------|
|  71 | getuid    | `uid_t(void)`            | sys_proc.c  |
|  72 | getgid    | `gid_t(void)`            | sys_proc.c  |
|  73 | geteuid   | `uid_t(void)`            | sys_proc.c  |
|  74 | getegid   | `gid_t(void)`            | sys_proc.c  |
|  75 | setuid    | `int(uid_t)`             | sys_proc.c  |
|  80 | setgid    | `int(gid_t)`             | sys_proc.c  |
|  81 | seteuid   | `int(uid_t)`             | sys_proc.c  |
| 106 | setegid   | `int(gid_t)`             | sys_proc.c  |

All credential edits go through `cred_set_*` in `src/kernel/core/cred.c`.

## Job control

| nr  | name        | signature                    |
|-----|-------------|------------------------------|
|  84 | setpgid     | `int(pid_t, pid_t)`          |
|  85 | getpgid     | `pid_t(pid_t)`               |
|  86 | getpgrp     | `pid_t(void)`                |
|  87 | setsid      | `pid_t(void)`                |
|  88 | getsid      | `pid_t(pid_t)`               |
|  89 | tcgetpgrp   | `pid_t(int fd)`              |
|  90 | tcsetpgrp   | `int(int fd, pid_t)`         |
|  91 | tcgetattr   | `int(int fd, struct termios *)`|
|  92 | tcsetattr   | `int(int fd, int actions, const struct termios *)`|
|  93 | isatty      | `int(int fd)`                |
|  94 | ttyname     | `int(int fd, char *buf, size_t len)`|

Lives in `src/kernel/core/jobctl.c`; syscalls are thin facades in
`sys_proc.c` and `sys_misc.c`.

## File descriptors

| nr  | name       | signature                                  |
|-----|------------|--------------------------------------------|
|  10 | read       | `ssize_t(int fd, void *, size_t)`          |
|  11 | write      | `ssize_t(int fd, const void *, size_t)`    |
|  12 | open       | `int(const char *, int flags, mode_t)`     |
|  13 | close      | `int(int fd)`                              |
|  14 | lseek      | `off_t(int fd, off_t, int whence)`         |
|  15 | dup        | `int(int fd)`                              |
|  16 | dup2       | `int(int old, int new)`                    |
|  17 | pipe       | `int(int pipefd[2])`                       |
|  20 | stat       | `int(const char *, struct stat *)`         |
|  21 | fstat      | `int(int fd, struct stat *)`               |
|  30 | getdents   | `int(int fd, void *buf, unsigned count)`   |

All wired in `src/kernel/syscalls/sys_fs.c`; back end in
`src/kernel/fs/vfs_{core,fd}.c`.

## Path operations

| nr  | name      | signature                              |
|-----|-----------|----------------------------------------|
|  22 | mkdir     | `int(const char *, mode_t)`            |
|  23 | rmdir     | `int(const char *)`                    |
|  24 | unlink    | `int(const char *)`                    |
|  25 | rename    | `int(const char *, const char *)`      |
|  26 | chmod     | `int(const char *, mode_t)`            |
|  27 | chown     | `int(const char *, uid_t, gid_t)`      |
|  31 | chdir     | `int(const char *)`                    |
|  32 | getcwd    | `int(char *buf, size_t)`               |
| 103 | truncate  | `int(const char *, off_t)`             |
| 104 | ftruncate | `int(int fd, off_t)`                   |

`__NR_link` (28) is declared in the UAPI for libc's stub, but is
intentionally not wired -- the dispatcher returns `-ENOSYS`.

## Memory

| nr | name      | signature                                                  |
|----|-----------|------------------------------------------------------------|
| 40 | brk       | `void *(void *)`                                           |
| 41 | mmap      | `void *(void *, size_t, int prot, int flags, int fd, off_t)`|
| 42 | munmap    | `int(void *, size_t)`                                      |
| 43 | mprotect  | `int(void *, size_t, int prot)`                            |

`PROT_*` and `MAP_*` come from `uapi/mman.h` (single source of truth).
`mmap` with `fd >= 0 && !(flags & MAP_ANON)` is lazy: pages are
populated on fault by `src/kernel/mm/mmap_file.c::handle_demand_fault`.

The VGA framebuffer (`/dev/fb` or `MAP_FIXED` to `VGA_FRAMEBUFFER_ADDR`)
is identity-mapped immediately at mmap time.

## Time

| nr | name           | signature                                                |
|----|----------------|----------------------------------------------------------|
| 60 | time           | `time_t(time_t *)`                                       |
| 61 | nanosleep      | `int(const struct timespec *, struct timespec *)`        |
| 62 | gettimeofday   | `int(struct timeval *, void *tz)`                        |
| 63 | clock_gettime  | `int(clockid_t, struct timespec *)`                      |

Backed by `src/kernel/drivers/timer/timer.c` (`time_now_*` /
`time_sleep_timespec`). Epoch is set at boot from CMOS RTC.

## Resource limits

| nr | name      | signature                                |
|----|-----------|------------------------------------------|
| 76 | getrlimit | `int(int resource, struct rlimit *)`     |
| 77 | setrlimit | `int(int resource, const struct rlimit *)`|
| 78 | nice      | `int(int inc)`                           |

`getrlimit/setrlimit/nice` go through ABI shims in `syscall_table.c`
because their native prototype isn't `(u32 x5)`.

## System & graphics

| nr  | name        | signature                          |
|-----|-------------|------------------------------------|
| 100 | reboot      | `int(int cmd)`                     |
| 245 | gfx_mode    | `int(int mode)`                    |
| 246 | gfx_palette | `int(uint32_t *, size_t, size_t)`  |
| 247 | kb_event    | `int(struct kb_event *)`           |

`gfx_*` and `kb_event` are Doom-side support primitives; backed by
`src/kernel/drivers/video/vga_graphics.c` and
`src/kernel/drivers/input/keyboard.c`.

## Error codes

`uapi/errno.h` exposes the standard POSIX set: EPERM, ENOENT, ESRCH,
EINTR, EIO, ENXIO, E2BIG, ENOEXEC, EBADF, ECHILD, EAGAIN, ENOMEM,
EACCES, EFAULT, ENOTBLK, EBUSY, EEXIST, EXDEV, ENODEV, ENOTDIR,
EISDIR, EINVAL, ENFILE, EMFILE, ENOTTY, EFBIG, ENOSPC, ESPIPE, EROFS,
EMLINK, EPIPE, ERANGE, ENAMETOOLONG, ENOSYS.

## Internal driver interfaces

These are not user-facing syscalls; they document the in-kernel module
seams that `init/main.c` calls during bring-up.

### Timer

```c
void pit_init(uint32_t hz);
uint64_t time_now_ms(void);
void time_now_timeval(struct k_timeval *);
void time_now_timespec(struct k_timespec *);
void time_sleep_timespec(const struct k_timespec *req, struct k_timespec *rem);
```

### Console + framebuffer

```c
void vga_init(void);
void vga_graphics_mode(int mode);
void vga_graphics_palette(const uint32_t *rgb, size_t off, size_t n);
```

### Block + filesystem

```c
int pci_enum(void);
int virtio_blk_probe(void);
int ext2_mount(block_device_t *);
int vfs_init(void);
```

### SMP

```c
void acpi_parse(void);
void lapic_init(void);
void ioapic_init(void);
void smp_start_aps(void);
struct per_cpu *this_cpu(void);
void smp_tlb_flush_all(void);
```
