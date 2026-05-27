/*
 * echo.c — prints argv[1..argc] separated by spaces, terminated by newline.
 *
 * Invariants:
 *  - Writes to fd 1 only; never reads from stdin.
 *  - Used by test_runner.c T11 (execve smoke test) — exit code 0 expected.
 *
 * Not allowed:
 *  - -n (suppress newline) or escape interpretation.
 */
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    puts(argv[i]);
    if (i < argc - 1) {
      write(1, " ", 1);
    }
  }
  write(1, "\n", 1);
  return 0;
}
