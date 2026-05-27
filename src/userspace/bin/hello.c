/*
 * hello.c — prints "hello from auto-discovered binary"; sentinel verifying
 * that any .c dropped into userspace/bin/ is picked up by the Makefile
 * wildcard and executable from /bin (used by test_runner.c T22).
 *
 * Invariants:
 *  - Exit code 0 on success; T22 checks exit code only.
 *
 * Not allowed:
 *  - Adding any logic — this file's value is its triviality.
 */
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  puts("hello from auto-discovered binary");
  return 0;
}
