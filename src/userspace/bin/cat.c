/*
 * cat.c — reads argv[1] in 256-byte chunks and writes them to stdout until EOF.
 *
 * Invariants:
 *  - argv[1] is expected to be an absolute path (sh resolves before exec).
 *  - fd is always closed on every exit path.
 *
 * Not allowed:
 *  - Multi-file concatenation or flag parsing (single positional arg only).
 */
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    puts("Usage: cat <file>");
    return 1;
  }

  const char *path = argv[1];

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    puts("cat: cannot open ");
    puts(path);
    return 1;
  }

  char buf[256];
  int n;
  while ((n = read(fd, buf, sizeof(buf))) > 0) {
    write(1, buf, n);
  }

  close(fd);
  return 0;
}
