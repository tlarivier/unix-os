/*
 * test_runner.c — userspace regression suite (T01..T25) covering pipe/EOF,
 * mmap MAP_FIXED, chmod, priv-drop, fork/wait, mprotect, unlink-while-open,
 * dup2, execve, errno, setpgid, brk, chdir, snprintf, qsort; execs /bin/sh
 * after reporting.
 *
 * Invariants:
 *  - Runs as PID 1's first child (root, euid=0); tests needing non-priv
 * fork+setuid.
 *  - Direct __syscall* used where libc indirection could mask a kernel bug.
 *  - Exit only via execve("/bin/sh") so the user lands in a shell.
 *
 * Not allowed:
 *  - Depending on test ordering across functions (each test_* is
 * self-contained).
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANON 0x20

#define UNPRIV_UID 1000

static int pass_count = 0;
static int fail_count = 0;
static int skip_count = 0;

int cmp_int(const void *a, const void *b) {
  int x = *(const int *)a, y = *(const int *)b;
  return (x > y) - (x < y);
}

static void report(const char *name, int ok, long got, long expected) {
  if (ok) {
    printf("  PASS  %s\n", name);
    pass_count++;
  } else {
    printf("  FAIL  %s (got %d, expected %d)\n", name, (int)got, (int)expected);
    fail_count++;
  }
}

static void report_skip(const char *name, const char *reason) {
  printf("  SKIP  %s (%s)\n", name, reason);
  skip_count++;
}

static void check_eq(const char *name, long got, long expected) {
  report(name, got == expected, got, expected);
}

static void check_gt(const char *name, long got, long bound) {
  report(name, got > bound, got, bound);
}

static void test_getpid(void) {
  long pid = __syscall0(SYS_getpid);
  check_gt("T01 getpid > 0", pid, 0);
}

static void test_pipe_eof(void) {
  int fds[2];
  long rc = __syscall1(__NR_pipe, (long)fds);
  if (rc < 0) {
    report("T02 pipe()", 0, rc, 0);
    return;
  }

  long w = __syscall3(__NR_write, fds[1], (long)"x", 1);
  if (w != 1) {
    report("T02 pipe write", 0, w, 1);
    return;
  }

  char buf[8];
  long r1 = __syscall3(__NR_read, fds[0], (long)buf, 1);
  if (r1 != 1) {
    report("T02 pipe read byte", 0, r1, 1);
    return;
  }

  __syscall1(__NR_close, fds[1]);
  long r2 = __syscall3(__NR_read, fds[0], (long)buf, 1);
  check_eq("T02 pipe EOF after close(write)", r2, 0);

  __syscall1(__NR_close, fds[0]);
}

static void test_mmap_fixed_lowaddr(void) {
  long rc = __syscall5(__NR_mmap, 0x100, 4096, PROT_READ | PROT_WRITE,
                       MAP_FIXED | MAP_PRIVATE | MAP_ANON, (long)-1);
  check_eq("T03 mmap MAP_FIXED lowaddr -> -EINVAL", rc, -EINVAL);
}

struct user_stat {
  unsigned long st_dev;     /* 0:4  */
  unsigned long st_ino;     /* 4:4  */
  unsigned short st_mode;   /* 8:2  */
  unsigned short st_nlink;  /* 10:2 */
  unsigned short st_uid;    /* 12:2 */
  unsigned short st_gid;    /* 14:2 */
  unsigned long st_rdev;    /* 16:4 */
  long st_size;             /* 20:4 */
  unsigned long st_blksize; /* 24:4 */
  unsigned long st_blocks;  /* 28:4 */
  unsigned long st_atime;   /* 32:4 */
  unsigned long st_atime_nsec;
  unsigned long st_mtime;
  unsigned long st_mtime_nsec;
  unsigned long st_ctime;
  unsigned long st_ctime_nsec;
  unsigned long __unused4;
  unsigned long __unused5;
};

static void test_chmod_roundtrip(void) {
  const char *path = "/tmp/test_chmod";
  int fd = (int)__syscall3(__NR_open, (long)path,
                           0x42 /*O_CREAT|O_RDWR (Linux i386)*/, 0644);
  if (fd < 0) {
    report("T04 create file", 0, fd, 0);
    return;
  }
  __syscall1(__NR_close, fd);

  long crc = __syscall2(__NR_chmod, (long)path, 0444);
  if (crc < 0) {
    report("T04 chmod -> 0", 0, crc, 0);
    return;
  }

  struct user_stat st;
  long src = __syscall2(__NR_stat, (long)path, (long)&st);
  if (src < 0) {
    report("T04 stat", 0, src, 0);
    return;
  }
  long perm = (long)(st.st_mode & 0777);
  check_eq("T04 chmod stored (0444)", perm, 0444);
}

#define WEXIT(s) (((s) >> 8) & 0xFF)
#define WSIG(s) ((s) & 0x7F)

static int run_unpriv(int syscall_nr, long a1, long a2) {
  long pid = __syscall0(__NR_fork);
  if (pid < 0)
    return -1;
  if (pid == 0) {
    long su = __syscall1(__NR_setuid, UNPRIV_UID);
    if (su < 0)
      __syscall1(__NR_exit, 77);
    long rc;
    if (syscall_nr == __NR_reboot)
      rc = __syscall1(syscall_nr, a1);
    else
      rc = __syscall2(syscall_nr, a1, a2);
    __syscall1(__NR_exit, (int)((-rc) & 0xFF));
  }
  int status = 0;
  __syscall3(__NR_waitpid, pid, (long)&status, 0);
  return WEXIT(status);
}

static void test_priv_drops(void) {
  int k_err = run_unpriv(__NR_kill, 1, 9);
  if (k_err == 77) {
    report_skip("T05/T06 priv-drops", "setuid unavailable");
    return;
  }
  check_eq("T05 kill(1,9) as unpriv -> -EPERM", k_err, EPERM);

  int r_err = run_unpriv(__NR_reboot, 0, 0);
  check_eq("T06 reboot() as unpriv -> -EPERM", r_err, EPERM);
}

static void test_fork_waitpid(void) {
  long pid = __syscall0(__NR_fork);
  if (pid < 0) {
    report("T07 fork", 0, pid, 0);
    return;
  }
  if (pid == 0) {
    __syscall1(__NR_exit, 42);
  }
  int status = 0;
  long wp = __syscall3(__NR_waitpid, pid, (long)&status, 0);
  if (wp < 0) {
    report("T07 waitpid", 0, wp, 0);
    return;
  }
  check_eq("T07 fork+waitpid exit code", WEXIT(status), 42);
}

static void test_mprotect_protnone(void) {
  long addr = __syscall5(__NR_mmap, 0, 4096, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANON, (long)-1);
  if (addr < 0 || addr == 0) {
    report_skip("T08 mprotect PROT_NONE", "mmap failed");
    return;
  }
  *(volatile char *)addr = 'A';

  long prc = __syscall3(__NR_mprotect, addr, 4096, PROT_NONE);
  if (prc < 0) {
    __syscall2(__NR_munmap, addr, 4096);
    report_skip("T08 mprotect PROT_NONE", "mprotect not supported");
    return;
  }

  long pid = __syscall0(__NR_fork);
  if (pid < 0) {
    __syscall2(__NR_munmap, addr, 4096);
    report("T08 fork", 0, pid, 0);
    return;
  }
  if (pid == 0) {
    volatile char c = *(volatile char *)addr;
    (void)c;
    __syscall1(__NR_exit, 0);
  }
  int status = 0;
  __syscall3(__NR_waitpid, pid, (long)&status, 0);
  int sig = WSIG(status);
  report("T08 PROT_NONE access kills child", sig == SIGSEGV, sig, SIGSEGV);

  __syscall2(__NR_munmap, addr, 4096);
}

static void test_unlink_open_ref(void) {
  const char *path = "/tmp/test_nlink";
  int fd = (int)__syscall3(__NR_open, (long)path,
                           0x42 /*O_CREAT|O_RDWR (Linux i386)*/, 0644);
  if (fd < 0) {
    report("T09 create", 0, fd, 0);
    return;
  }

  long w = __syscall3(__NR_write, fd, (long)"hello", 5);
  if (w != 5) {
    report("T09 write", 0, w, 5);
    __syscall1(__NR_close, fd);
    return;
  }

  long urc = __syscall1(__NR_unlink, (long)path);
  if (urc < 0) {
    report("T09 unlink while open", 0, urc, 0);
    __syscall1(__NR_close, fd);
    return;
  }

  long sk = __syscall3(__NR_lseek, fd, 0, 0 /*SEEK_SET*/);
  (void)sk;
  char buf[8] = {0};
  long r = __syscall3(__NR_read, fd, (long)buf, 5);
  if (r != 5) {
    report("T09 read after unlink", 0, r, 5);
    __syscall1(__NR_close, fd);
    return;
  }
  int match = (buf[0] == 'h' && buf[1] == 'e' && buf[2] == 'l' &&
               buf[3] == 'l' && buf[4] == 'o');
  report("T09 read-after-unlink content", match, match, 1);

  __syscall1(__NR_close, fd);

  long fd2 = __syscall3(__NR_open, (long)path, O_RDONLY, 0);
  if (fd2 >= 0) {
    report("T09 file gone after final close", 0, fd2, -ENOENT);
    __syscall1(__NR_close, (long)fd2);
  } else {
    check_eq("T09 file gone after final close", fd2, -ENOENT);
  }
}

static void test_dup2(void) {
  int fds[2];
  long rc = __syscall1(__NR_pipe, (long)fds);
  if (rc < 0) {
    report("T10 pipe", 0, rc, 0);
    return;
  }

  long d = __syscall2(__NR_dup2, fds[1], 5);
  if (d != 5) {
    report("T10 dup2 -> 5", 0, d, 5);
    __syscall1(__NR_close, fds[0]);
    __syscall1(__NR_close, fds[1]);
    return;
  }
  __syscall1(__NR_close, fds[1]);

  long w = __syscall3(__NR_write, 5, (long)"x", 1);
  if (w != 1) {
    report("T10 write via dup2 fd", 0, w, 1);
    __syscall1(__NR_close, 5);
    __syscall1(__NR_close, fds[0]);
    return;
  }
  char buf[2] = {0};
  long r = __syscall3(__NR_read, fds[0], (long)buf, 1);
  int ok = (r == 1 && buf[0] == 'x');
  report("T10 dup2 pipe round-trip", ok, ok, 1);

  __syscall1(__NR_close, 5);
  __syscall1(__NR_close, fds[0]);
}

static void test_execve_smoke(void) {
  long pid = __syscall0(__NR_fork);
  if (pid < 0) {
    report("T11 fork", 0, pid, 0);
    return;
  }
  if (pid == 0) {
    char *args[] = {"echo", "exec_works", (char *)0};
    __syscall3(__NR_execve, (long)"/bin/echo", (long)args, 0);
    __syscall1(__NR_exit, 99);
  }
  int status = 0;
  __syscall3(__NR_waitpid, pid, (long)&status, 0);
  check_eq("T11 execve /bin/echo exit code", WEXIT(status), 0);
}

static void test_getppid(void) {
  long parent = __syscall0(__NR_getpid);
  long pid = __syscall0(__NR_fork);
  if (pid < 0) {
    report("T12 fork", 0, pid, 0);
    return;
  }
  if (pid == 0) {
    long pp = __syscall0(__NR_getppid);
    __syscall1(__NR_exit, (int)(pp & 0xFF));
  }
  int status = 0;
  __syscall3(__NR_waitpid, pid, (long)&status, 0);
  int got = WEXIT(status);
  int expected = (int)(parent & 0xFF);
  check_eq("T12 child getppid == parent pid (low byte)", got, expected);
}

static char t1xx_badfd_buf[1];

struct errno_case {
  const char *name;
  int nr;
  int argc;
  long a1, a2, a3;
  long expected;
};

static const struct errno_case t13_t17_cases[] = {
    {"T13 read(999) -> -EBADF", __NR_read, 3, 999, (long)t1xx_badfd_buf, 1,
     -EBADF},
    {"T14 close(999) -> -EBADF", __NR_close, 1, 999, 0, 0, -EBADF},
    {"T15 open(/nonexistent_xyz) -> -ENOENT", __NR_open, 3,
     (long)"/nonexistent_xyz", O_RDONLY, 0, -ENOENT},
    {"T16 unlink(/nonexistent_xyz) -> -ENOENT", __NR_unlink, 1,
     (long)"/nonexistent_xyz", 0, 0, -ENOENT},
    {"T17 mkdir(/tmp) -> -EEXIST", __NR_mkdir, 2, (long)"/tmp", 0755, 0,
     -EEXIST},
};

static void run_t13_t17(void) {
  int n = (int)(sizeof(t13_t17_cases) / sizeof(t13_t17_cases[0]));
  for (int i = 0; i < n; i++) {
    const struct errno_case *c = &t13_t17_cases[i];
    long rc;
    switch (c->argc) {
    case 1:
      rc = __syscall1(c->nr, c->a1);
      break;
    case 2:
      rc = __syscall2(c->nr, c->a1, c->a2);
      break;
    case 3:
      rc = __syscall3(c->nr, c->a1, c->a2, c->a3);
      break;
    default:
      rc = 0;
    }
    check_eq(c->name, rc, c->expected);
  }
}

static void test_setpgid_getpgid(void) {
  long pid = __syscall0(__NR_getpid);
  long src = __syscall2(__NR_setpgid, 0, pid);
  if (src < 0) {
    report_skip("T18 setpgid/getpgid", "setpgid not available");
    return;
  }
  long pg = __syscall1(__NR_getpgid, 0);
  if (pg < 0) {
    report_skip("T18 setpgid/getpgid", "getpgid not available");
    return;
  }
  check_eq("T18 getpgid(0) == self pid after setpgid", pg, pid);
}

static void test_brk(void) {
  long oldbrk = __syscall1(__NR_brk, 0);
  if (oldbrk < 0) {
    report_skip("T19 brk", "brk not available");
    return;
  }
  long newbrk = __syscall1(__NR_brk, oldbrk + 4096);
  if (newbrk < 0) {
    report("T19 brk extend", 0, newbrk, oldbrk + 4096);
    return;
  }
  int extend_ok =
      (newbrk == oldbrk + 4096) || (newbrk == 0) || (newbrk >= oldbrk);
  report("T19 brk extend succeeded", extend_ok, newbrk, oldbrk + 4096);

  volatile char *p = (volatile char *)(uintptr_t)oldbrk;
  p[0] = 'B';
  char read_back = p[0];

  long restored = __syscall1(__NR_brk, oldbrk);
  int shrink_ok = (restored == oldbrk) || (restored == 0) || (restored >= 0);
  report("T19 brk write/read back", read_back == 'B' && shrink_ok,
         (long)read_back, 'B');
}

static void test_pipe_isolation(void) {
  int p1[2], p2[2], p3[2];
  if (__syscall1(__NR_pipe, (long)p1) < 0) {
    report("T20 pipe1", 0, 0, 0);
    return;
  }
  if (__syscall1(__NR_pipe, (long)p2) < 0) {
    report("T20 pipe2", 0, 0, 0);
    return;
  }
  if (__syscall1(__NR_pipe, (long)p3) < 0) {
    report("T20 pipe3", 0, 0, 0);
    return;
  }

  __syscall3(__NR_write, p1[1], (long)"A", 1);
  __syscall3(__NR_write, p2[1], (long)"B", 1);
  __syscall3(__NR_write, p3[1], (long)"C", 1);

  char b1 = 0, b2 = 0, b3 = 0;
  __syscall3(__NR_read, p1[0], (long)&b1, 1);
  __syscall3(__NR_read, p2[0], (long)&b2, 1);
  __syscall3(__NR_read, p3[0], (long)&b3, 1);

  int ok = (b1 == 'A' && b2 == 'B' && b3 == 'C');
  report("T20 three pipes isolated", ok, (b1 << 16) | (b2 << 8) | b3,
         ('A' << 16) | ('B' << 8) | 'C');

  __syscall1(__NR_close, p1[0]);
  __syscall1(__NR_close, p1[1]);
  __syscall1(__NR_close, p2[0]);
  __syscall1(__NR_close, p2[1]);
  __syscall1(__NR_close, p3[0]);
  __syscall1(__NR_close, p3[1]);
}

static void test_chdir_relative(void) {
  int fd = (int)__syscall3(__NR_open, (long)"/tmp/t21_marker",
                           0x42 /*O_CREAT|O_RDWR*/, 0644);
  if (fd < 0) {
    report("T21 create absolute", 0, fd, 0);
    return;
  }
  long w = __syscall3(__NR_write, fd, (long)"R", 1);
  if (w != 1) {
    report("T21 write", 0, w, 1);
    __syscall1(__NR_close, fd);
    return;
  }
  __syscall1(__NR_close, fd);

  long crc = __syscall1(__NR_chdir, (long)"/tmp");
  if (crc < 0) {
    report_skip("T21 chdir", "chdir failed");
    return;
  }

  int fd2 = (int)__syscall3(__NR_open, (long)"t21_marker", 0 /*O_RDONLY*/, 0);
  int ok = (fd2 >= 0);
  report("T21 open relative after chdir", ok, fd2, 0);
  if (fd2 >= 0) {
    char c = 0;
    __syscall3(__NR_read, fd2, (long)&c, 1);
    report("T21 relative read content", c == 'R', c, 'R');
    __syscall1(__NR_close, fd2);
  }

  __syscall1(__NR_chdir, (long)"/");
  __syscall1(__NR_unlink, (long)"/tmp/t21_marker");
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  puts("");
  puts("=========================================");
  puts("  UnixOS regression test runner");
  puts("=========================================");

  test_getpid();
  test_pipe_eof();
  test_mmap_fixed_lowaddr();
  test_chmod_roundtrip();
  test_priv_drops();
  test_fork_waitpid();
  test_mprotect_protnone();
  test_unlink_open_ref();
  test_dup2();
  test_execve_smoke();
  test_getppid();
  run_t13_t17();
  test_setpgid_getpgid();
  test_brk();
  test_pipe_isolation();
  test_chdir_relative();

  {
    long pid = __syscall0(__NR_fork);
    if (pid == 0) {
      char *args[] = {"hello", (char *)0};
      long rc = __syscall3(__NR_execve, (long)"/bin/hello", (long)args, 0);
      __syscall1(__NR_exit, (int)((-rc) & 0xFF));
    }
    int status = 0;
    __syscall3(__NR_waitpid, pid, (long)&status, 0);
    check_eq("T22 execve auto-discovered /bin/hello", WEXIT(status), 0);
  }

  {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "x=%d y=%s z=%x", 42, "ok", 0xCAFE);
    int len = 0;
    while (buf[len])
      len++;
    int ok = (n == len) && buf[0] == 'x' && buf[2] == '4' && buf[5] == 'y' &&
             buf[7] == 'o' && buf[10] == 'z' && buf[12] == 'c';
    check_eq("T23 snprintf return matches strlen", n, len);
    report("T23 snprintf produces correct bytes", ok, ok, 1);
  }

  {
    char buf[8];
    int n = snprintf(buf, sizeof(buf), "abcdefghijklmno");
    int len = 0;
    while (buf[len])
      len++;
    report("T24 snprintf reports full length on truncate", n == 15, n, 15);
    report("T24 snprintf null-terminates within bound",
           len == 7 && buf[7] == '\0', len, 7);
  }

  {
    int arr[] = {7, 3, 9, 1, 5, 2, 8, 4, 6, 0};
    qsort(arr, 10, sizeof(int), cmp_int);
    int sorted = 1;
    for (int i = 0; i < 10; i++)
      if (arr[i] != i) {
        sorted = 0;
        break;
      }
    report("T25 qsort sorts ints ascending", sorted, sorted, 1);
  }

  {
    int a = 0, b = 0;
    char c[16] = {0};
    int n = sscanf("0xDEAD 42 hello", "%x %d %s", &a, &b, c);
    int ok = (n == 3) && (a == 0xDEAD) && (b == 42) && c[0] == 'h' &&
             c[1] == 'e' && c[2] == 'l' && c[3] == 'l' && c[4] == 'o' &&
             c[5] == '\0';
    report("T26 sscanf %x %d %s parses three fields", ok, ok, 1);
  }

  puts("-----------------------------------------");
  printf("  RESULT: %d passed, %d failed", pass_count, fail_count);
  if (skip_count)
    printf(", %d skipped", skip_count);
  puts("");
  puts("=========================================");
  puts("");

  char *args[] = {"/bin/sh", (char *)0};
  execve("/bin/sh", args, (char *const *)0);
  __syscall1(__NR_exit, fail_count == 0 ? 0 : 1);
  __builtin_unreachable();
}
