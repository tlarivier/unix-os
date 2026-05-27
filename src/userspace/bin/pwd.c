/*
 * pwd.c — calls getcwd() and prints the result (fallback "/" on failure).
 *
 * Invariants:
 *  - Buffer is 64 bytes; longer cwd is silently truncated by getcwd().
 *  - Duplicates the sh.c builtin so execve("/bin/pwd") works standalone.
 *
 * Not allowed:
 *  - -L / -P flag handling.
 */
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  char cwd[64];
  if (getcwd(cwd, sizeof(cwd)) >= 0) {
    puts(cwd);
  } else {
    puts("/");
  }
  return 0;
}
