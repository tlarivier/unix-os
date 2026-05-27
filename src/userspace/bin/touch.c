/*
 * touch.c — open(argv[1], O_CREAT|O_WRONLY) then close; creates the file
 * if missing (does not update timestamps — no utimes syscall yet).
 *
 * Invariants:
 *  - argv[1] is expected to be an absolute path (sh resolves before exec).
 *  - fd is closed on every exit path.
 *
 * Not allowed:
 *  - Updating atime/mtime (kernel lacks utimes/utimensat — diverges from POSIX
 * touch).
 */
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    puts("Usage: touch <file>");
    return 1;
  }

  const char *path = argv[1];

  int fd = open(path, O_CREAT | O_WRONLY);
  if (fd < 0) {
    puts("touch: cannot create ");
    puts(path);
    return 1;
  }

  close(fd);
  return 0;
}
