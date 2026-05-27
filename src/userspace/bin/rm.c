/*
 * rm.c — unlink(argv[1]) and report errors.
 *
 * Invariants:
 *  - argv[1] is expected to be an absolute path (sh resolves before exec).
 *  - Exit status is 1 on any unlink failure, 0 otherwise.
 *
 * Not allowed:
 *  - Recursive removal or flag handling (no -r, -f).
 */
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    puts("Usage: rm <file>");
    return 1;
  }

  const char *path = argv[1];

  int ret = unlink(path);
  if (ret < 0) {
    puts("rm: cannot remove ");
    puts(path);
    return 1;
  }

  return 0;
}
