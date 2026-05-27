/*
 * init.c — PID 1: fork+exec /sbin/test_runner (fallback /bin/sh), reap, then
 * perpetually respawn /bin/sh.
 *
 * Invariants:
 *  - Runs as PID 1 with euid=0; the kernel execs this binary at boot.
 *  - On any child exit, loop respawns /bin/sh so the system never lacks a
 * shell.
 *
 * Not allowed:
 *  - Calling _exit() outside of the unrecoverable fork-failure path.
 */
#include <stdio.h>
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  puts("UnixOS Init (PID 1)");

  const char *first_prog = "/sbin/test_runner";

  while (1) {
    int pid = fork();

    if (pid < 0) {
      puts("init: fork failed");
      _exit(1);
    }

    if (pid == 0) {
      write(1, "CHILD: before execve\n", 21);
      char *args[] = {(char *)first_prog, (char *)0};
      execve(first_prog, args, (char *const *)0);
      /* Fallback: if /sbin/test_runner is missing, exec the shell. */
      char *sh_args[] = {"/bin/sh", (char *)0};
      execve("/bin/sh", sh_args, (char *const *)0);
      write(1, "CHILD: execve failed!\n", 22);
      _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    puts("init: child exited, restarting shell...");
    first_prog = "/bin/sh";
  }

  return 0;
}
