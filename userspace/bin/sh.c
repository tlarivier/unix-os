#include <sys/types.h>
#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Current working directory */
static char cwd[256] = "/";

/* Update cwd from kernel */
static void update_cwd(void) {
    getcwd(cwd, sizeof(cwd));
    if (cwd[0] == '\0') cwd[0] = '/', cwd[1] = '\0';
}

/* Resolve a path relative to cwd into absolute path */
static void resolve_path(const char* path, char* out, int outlen) {
    if (!path || !out || outlen < 2) return;
    
    /* Already absolute */
    if (path[0] == '/') {
        int i = 0;
        while (path[i] && i < outlen - 1) {
            out[i] = path[i];
            i++;
        }
        out[i] = '\0';
        return;
    }
    
    /* Build absolute path: cwd + "/" + path */
    int i = 0;
    const char* c = cwd;
    while (*c && i < outlen - 1) out[i++] = *c++;
    
    /* Add separator if needed */
    if (i > 0 && out[i-1] != '/' && i < outlen - 1) out[i++] = '/';
    
    while (*path) {
        if (path[0] == '.' && path[1] == '.' && (path[2] == '/' || path[2] == '\0')) {
            /* Go up one directory */
            if (i > 1) {
                i--;  /* Remove trailing slash */
                while (i > 0 && out[i-1] != '/') i--;
            }
            path += 2;
            if (*path == '/') path++;
        } else if (path[0] == '.' && (path[1] == '/' || path[1] == '\0')) {
            /* Current dir - skip */
            path++;
            if (*path == '/') path++;
        } else {
            while (*path && *path != '/' && i < outlen - 1) out[i++] = *path++;
            if (*path == '/' && i < outlen - 1) out[i++] = *path++;
        }
    }
    
    if (i > 1 && out[i-1] == '/') i--;
    out[i] = '\0';
    
    if (out[0] == '\0') out[0] = '/', out[1] = '\0';
}

/* Parse command line into argv */
/* must have a regex parser */
static int parse_args(char* cmd, char** argv, int max_args) {
    int argc = 0;
    char* p = cmd;
    
    while (*p && argc < max_args - 1) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        
        argv[argc++] = p;
        
        /* Find end of argument */
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    argv[argc] = (char*)0;
    return argc;
}

/* Build path for command */
static int build_cmd_path(const char* cmd, char* path, int pathlen) {
    /* Check if already absolute */
    if (cmd[0] == '/') {
        int i = 0;
        while (cmd[i] && i < pathlen - 1) {
            path[i] = cmd[i];
            i++;
        }
        path[i] = '\0';
        return i;
    }
    
    /* Prepend /bin/ */
    const char* prefix = "/bin/";
    int i = 0;
    while (*prefix && i < pathlen - 1) path[i++] = *prefix++;
    while (*cmd && i < pathlen - 1) path[i++] = *cmd++;
    path[i] = '\0';
    return i;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    puts("UnixOS Shell");
    
    /* Initialize cwd */
    update_cwd();
    
    char buf[128];
    char* args[16];
    
    while (1) {
        puts("> ");
        
        /* Read command */
        int pos = 0;
        while (pos < 127) {
            char c;
            int n = read(0, &c, 1);
            if (n <= 0) continue;
            
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
        
        if (pos == 0) continue;
        
        /* Parse arguments first */
        int nargs = parse_args(buf, args, 16);
        if (nargs == 0) continue;
        
        /* Built-in: exit */
        if (strcmp(args[0], "exit") == 0) {
            puts("Goodbye!");
            _exit(0);
        }
        
        /* Built-in: help */
        if (strcmp(args[0], "help") == 0) {
            puts("Built-in: help, exit, clear, cd, pwd");
            puts("External: ls, mkdir, cat, rm, echo, touch");
            continue;
        }
        
        /* Built-in: clear */
        if (strcmp(args[0], "clear") == 0) {
            puts("\033[2J\033[H");
            continue;
        }
        
        /* Built-in: cd */
        if (strcmp(args[0], "cd") == 0) {
            char target[128];
            if (nargs < 2) {
                target[0] = '/';
                target[1] = '\0';
            } else {
                resolve_path(args[1], target, sizeof(target));
            }
            if (chdir(target) < 0) {
                puts("cd: no such directory: ");
                puts(target);
            }
            update_cwd();
            continue;
        }
        
        /* Built-in: pwd */
        if (strcmp(args[0], "pwd") == 0) {
            update_cwd();
            puts(cwd);
            continue;
        }
        
        /* Resolve relative paths in arguments (skip command name) */
        static char resolved_args[8][128];
        static char* resolved_ptrs[16];
        int ri = 0;
        
        for (int i = 0; i < nargs && i < 16; i++) {
            if (i == 0) {
                /* Command name - don't resolve */
                resolved_ptrs[i] = args[i];
            } else if (args[i][0] == '-') {
                /* Option flag - don't resolve */
                resolved_ptrs[i] = args[i];
            } else if (args[i][0] == '.' && (args[i][1] == '/' || args[i][1] == '.') && ri < 8) {
                /* Only resolve paths starting with ./ or .. */
                resolve_path(args[i], resolved_args[ri], 128);
                resolved_ptrs[i] = resolved_args[ri];
                ri++;
            } else {
                /* Pass argument as-is - let the command handle it */
                resolved_ptrs[i] = args[i];
            }
        }
        resolved_ptrs[nargs] = (char*)0;
        
        /* Build command path */
        char cmdpath[64];
        build_cmd_path(args[0], cmdpath, sizeof(cmdpath));
        
        /* Fork and exec */
        int pid = fork();
        if (pid < 0) {
            puts("sh: fork failed");
            continue;
        }
        
        if (pid == 0) {
            /* Child process - use resolved paths */
            execve(cmdpath, resolved_ptrs, (char* const*)0);
            puts("sh: command not found: ");
            puts(args[0]);
            _exit(127);
        }
        
        /* Parent: wait for child */
        int status = 0;
        waitpid(pid, &status, 0);
    }
    
    return 0;
}
