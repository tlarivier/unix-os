/*
 * mkdir.c — mkdir(argv[1], 0755) and report the created path.
 *
 * Invariants:
 *  - argv[1] is expected to be an absolute path (sh resolves before exec).
 *  - Mode is fixed at 0755; umask is not consulted.
 *
 * Not allowed:
 *  - -p (parent creation) or -m mode flag.
 */
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    puts("Usage: mkdir <path>");
    return 1;
  }

  const char *path = argv[1];

  int ret = mkdir(path, 0755);
  if (ret < 0) {
    puts("mkdir: failed");
    return 1;
  }

  puts("Created: ");
  puts(path);
  return 0;
}
