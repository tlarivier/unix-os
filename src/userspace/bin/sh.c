/*
 * sh.c — minimal interactive shell: reads a line, dispatches builtins
 * (cd/pwd/exit/help/clear), resolves relative paths, and fork/execs a command
 * (or two joined by a single pipe).
 *
 * Invariants:
 *  - cwd[] mirrors the kernel cwd via getcwd() after every chdir.
 *  - find_pipe() runs before parse_args() on each half; both mutate buf in
 * place.
 *  - Argument count is capped (nargs <= 15) to bound resolved_ptrs[].
 *
 * Not allowed:
 *  - Multi-pipe pipelines, job control, redirections (single '|' only).
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

static char cwd[256] = "/";

static void update_cwd(void) {
  getcwd(cwd, sizeof(cwd));
  if (cwd[0] == '\0')
    cwd[0] = '/', cwd[1] = '\0';
}

static void resolve_path(const char *path, char *out, int outlen) {
  if (!path || !out || outlen < 2)
    return;

  if (path[0] == '/') {
    int i = 0;
    while (path[i] && i < outlen - 1) {
      out[i] = path[i];
      i++;
    }
    out[i] = '\0';
    return;
  }

  int i = 0;
  const char *c = cwd;
  while (*c && i < outlen - 1)
    out[i++] = *c++;

  if (i > 0 && out[i - 1] != '/' && i < outlen - 1)
    out[i++] = '/';

  while (*path) {
    if (path[0] == '.' && path[1] == '.' &&
        (path[2] == '/' || path[2] == '\0')) {
      if (i > 1) {
        i--;
        while (i > 0 && out[i - 1] != '/')
          i--;
      }
      path += 2;
      if (*path == '/')
        path++;
    } else if (path[0] == '.' && (path[1] == '/' || path[1] == '\0')) {
      path++;
      if (*path == '/')
        path++;
    } else {
      while (*path && *path != '/' && i < outlen - 1)
        out[i++] = *path++;
      if (*path == '/' && i < outlen - 1)
        out[i++] = *path++;
    }
  }

  if (i > 1 && out[i - 1] == '/')
    i--;
  out[i] = '\0';

  if (out[0] == '\0')
    out[0] = '/', out[1] = '\0';
}

static int parse_args(char *cmd, char **argv, int max_args) {
  int argc = 0;
  char *p = cmd;

  while (*p && argc < max_args - 1) {
    while (*p == ' ' || *p == '\t')
      p++;
    if (!*p)
      break;

    argv[argc++] = p;

    while (*p && *p != ' ' && *p != '\t')
      p++;
    if (*p)
      *p++ = '\0';
  }
  argv[argc] = (char *)0;
  return argc;
}

static int build_cmd_path(const char *cmd, char *path, int pathlen) {
  if (cmd[0] == '/') {
    int i = 0;
    while (cmd[i] && i < pathlen - 1) {
      path[i] = cmd[i];
      i++;
    }
    path[i] = '\0';
    return i;
  }

  const char *prefix = "/bin/";
  int i = 0;
  while (*prefix && i < pathlen - 1)
    path[i++] = *prefix++;
  while (*cmd && i < pathlen - 1)
    path[i++] = *cmd++;
  path[i] = '\0';
  return i;
}

static void resolve_argv(char **args, int nargs, char resolved_args[][128],
                         int resolved_pool_size, char **resolved_ptrs) {
  int ri = 0;
  for (int i = 0; i < nargs; i++) {
    if (i == 0) {
      resolved_ptrs[i] = args[i];
    } else if (args[i][0] == '-') {
      resolved_ptrs[i] = args[i];
    } else if (args[i][0] == '.' && (args[i][1] == '/' || args[i][1] == '.') &&
               ri < resolved_pool_size) {
      resolve_path(args[i], resolved_args[ri], 128);
      resolved_ptrs[i] = resolved_args[ri];
      ri++;
    } else {
      resolved_ptrs[i] = args[i];
    }
  }
  resolved_ptrs[nargs] = (char *)0;
}

static char *find_pipe(char *buf) {
  for (char *p = buf; *p; p++) {
    if (*p != '|')
      continue;
    if (p == buf)
      continue;
    char lprev = *(p - 1);
    if (lprev != ' ' && lprev != '\t')
      continue;
    char rnext = *(p + 1);
    if (rnext != ' ' && rnext != '\t' && rnext != '\0')
      continue;

    char *end_left = p - 1;
    while (end_left >= buf && (*end_left == ' ' || *end_left == '\t'))
      end_left--;
    *(end_left + 1) = '\0';

    char *right = p + 1;
    while (*right == ' ' || *right == '\t')
      right++;
    if (*right == '\0')
      return NULL;
    return right;
  }
  return NULL;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  puts("UnixOS Shell");

  update_cwd();

  char buf[128];
  char *args[16];
  char *args_r[16];

  while (1) {
    puts("> ");

    int pos = 0;
    while (pos < 127) {
      char c;
      int n = read(0, &c, 1);
      if (n <= 0)
        continue;

      if (c == '\b' || c == 127) {
        if (pos > 0) {
          pos--;
          write(1, "\b \b", 3);
        }
        continue;
      }

      write(1, &c, 1);

      if (c == '\n' || c == '\r') {
        buf[pos] = '\0';
        break;
      }
      buf[pos++] = c;
    }
    buf[pos] = '\0';

    if (pos == 0)
      continue;

    char *right_cmd = find_pipe(buf);

    int nargs = parse_args(buf, args, 16);
    if (nargs == 0)
      continue;
    if (nargs > 15)
      nargs = 15;

    if (!right_cmd) {
      const char *cmd = args[0];
      if (strcmp(cmd, "exit") == 0) {
        puts("Goodbye!");
        _exit(0);
      }
      if (strcmp(cmd, "help") == 0) {
        puts("Built-in: help, exit, clear, cd, pwd");
        puts("External: ls, mkdir, cat, rm, echo, touch");
        continue;
      }
      if (strcmp(cmd, "clear") == 0) {
        puts("\033[2J\033[H");
        continue;
      }
      if (strcmp(cmd, "pwd") == 0) {
        update_cwd();
        puts(cwd);
        continue;
      }
      if (strcmp(cmd, "cd") == 0) {
        char target[128];
        if (nargs < 2) {
          target[0] = '/';
          target[1] = '\0';
        } else
          resolve_path(args[1], target, sizeof(target));
        if (chdir(target) < 0) {
          puts("cd: no such directory: ");
          puts(target);
        }
        update_cwd();
        continue;
      }
    }

    static char resolved_args[8][128];
    static char *resolved_ptrs[16];
    resolve_argv(args, nargs, resolved_args, 8, resolved_ptrs);

    char cmdpath[64];
    build_cmd_path(args[0], cmdpath, sizeof(cmdpath));

    if (!right_cmd) {
      int pid = fork();
      if (pid < 0) {
        puts("sh: fork failed");
        continue;
      }

      if (pid == 0) {
        execve(cmdpath, resolved_ptrs, (char *const *)0);
        puts("sh: command not found: ");
        puts(args[0]);
        _exit(127);
      }

      int status = 0;
      waitpid(pid, &status, 0);
      continue;
    }

    int nargs_r = parse_args(right_cmd, args_r, 16);
    if (nargs_r == 0) {
      puts("sh: parse error near '|'");
      continue;
    }
    if (nargs_r > 15)
      nargs_r = 15;

    static char resolved_args_r[8][128];
    static char *resolved_ptrs_r[16];
    resolve_argv(args_r, nargs_r, resolved_args_r, 8, resolved_ptrs_r);

    char cmdpath_r[64];
    build_cmd_path(args_r[0], cmdpath_r, sizeof(cmdpath_r));

    int pfd[2];
    if (pipe(pfd) < 0) {
      puts("sh: pipe failed");
      continue;
    }

    int pid_l = fork();
    if (pid_l < 0) {
      close(pfd[0]);
      close(pfd[1]);
      puts("sh: fork failed");
      continue;
    }
    if (pid_l == 0) {
      dup2(pfd[1], 1);
      close(pfd[0]);
      close(pfd[1]);
      execve(cmdpath, resolved_ptrs, (char *const *)0);
      puts("sh: command not found: ");
      puts(args[0]);
      _exit(127);
    }

    int pid_r = fork();
    if (pid_r < 0) {
      close(pfd[0]);
      close(pfd[1]);
      int st;
      waitpid(pid_l, &st, 0);
      puts("sh: fork failed");
      continue;
    }
    if (pid_r == 0) {
      dup2(pfd[0], 0);
      close(pfd[0]);
      close(pfd[1]);
      execve(cmdpath_r, resolved_ptrs_r, (char *const *)0);
      puts("sh: command not found: ");
      puts(args_r[0]);
      _exit(127);
    }

    close(pfd[0]);
    close(pfd[1]);
    int st_l = 0, st_r = 0;
    waitpid(pid_l, &st_l, 0);
    waitpid(pid_r, &st_r, 0);
  }

  return 0;
}
