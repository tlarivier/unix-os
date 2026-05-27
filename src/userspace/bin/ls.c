/*
 * ls.c — lists a directory by walking the getdents() linux_dirent buffer and
 * printing d_name bounded by d_reclen so headers and padding never reach
 * stdout.
 *
 * Invariants:
 *  - Reads via syscall_opendir/readdir (libc wrapper) and writes to fd 1.
 *  - d_name is read only up to d_reclen - DNAME_OFF; no fixed-length
 * assumption.
 *
 * Not allowed:
 *  - Emitting the raw getdents buffer (would leak ino/off/reclen bytes).
 */
#include <../uapi/syscalls.h>
#include <stddef.h>
#include <stdint.h>

#include <userspace/libc.h>

#define DNAME_OFF 10

static size_t bounded_strlen(const char *s, size_t max) {
  size_t n = 0;
  while (n < max && s[n] != '\0')
    n++;
  return n;
}

int main(int argc, char *argv[]) {
  const char *path = ".";

  if (argc > 1) {
    path = argv[1];
  }

  int fd = syscall_opendir(path);
  if (fd < 0) {
    syscall_write(2, "ls: cannot access '", 19);
    syscall_write(2, path, strlen(path));
    const char *msg2 = "': No such file or directory\n";
    syscall_write(2, msg2, strlen(msg2));
    return 1;
  }

  char buffer[1024];
  while (1) {
    ssize_t n = syscall_readdir(fd, buffer, sizeof(buffer));
    if (n <= 0)
      break;

    size_t off = 0;
    while (off + DNAME_OFF + 1 <= (size_t)n) {
      uint16_t reclen = (uint16_t)((unsigned char)buffer[off + 8] |
                                   ((unsigned char)buffer[off + 9] << 8));
      if (reclen < DNAME_OFF + 1 || off + reclen > (size_t)n) {
        break;
      }

      const char *name = buffer + off + DNAME_OFF;
      size_t name_max = (size_t)reclen - DNAME_OFF;
      size_t name_len = bounded_strlen(name, name_max);

      if (name_len > 0) {
        syscall_write(1, name, name_len);
        syscall_write(1, "\n", 1);
      }

      off += reclen;
    }
  }

  syscall_close(fd);
  return 0;
}
