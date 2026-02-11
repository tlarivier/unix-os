#include <stdint.h>
#include <stddef.h>
#include <../uapi/syscalls.h>

#include <userspace/libc.h>

int main(int argc, char* argv[]) {
    const char* path = ".";
    
    if (argc > 1) {
        path = argv[1];
    }
    
    // Map "." to root since this OS has no current working directory yet
    if (path && path[0] == '.' && path[1] == '\0') {
        path = "/";
    }
    // Reject other relative paths (keep kernel strictness)
    if (path && path[0] != '/') {
        const char* msg = "ls: relative paths unsupported; use absolute path\n";
        syscall_write(2, msg, strlen(msg));
        return 1;
    }
    // Open directory
    int fd = syscall_opendir(path);
    if (fd < 0) {
        syscall_write(2, "ls: cannot access '", 19);
        syscall_write(2, path, strlen(path));
        const char* msg2 = "': No such file or directory\n";
        syscall_write(2, msg2, strlen(msg2));
        return 1;
    }
    
    // Read directory entries
    char buffer[256];
    while (1) {
        ssize_t n = syscall_readdir(fd, buffer, sizeof(buffer));
        if (n <= 0) break;
        syscall_write(1, buffer, (size_t)n);
        syscall_write(1, "\n", 1);
    }
    
    syscall_close(fd);
    return 0;
}
