/*
 * rmdir.c — rmdir(argv[1]) and report errors.
 *
 * Invariants:
 *  - argv[1] may be relative; the kernel resolves it via the calling
 *    process's cwd.
 *  - Exit status is 1 on any rmdir failure, 0 otherwise.
 *
 * Not allowed:
 *  - Recursive removal (no -p / -r); rmdir refuses non-empty dirs.
 */
#include <stdio.h>
#include <syscall.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    puts("Usage: rmdir <dir>");
    return 1;
  }

  if (rmdir(argv[1]) < 0) {
    puts("rmdir: cannot remove ");
    puts(argv[1]);
    return 1;
  }

  return 0;
}
