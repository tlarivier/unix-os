#include <sys/types.h>
#include <syscall.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        puts("Usage: cat <file>");
        return 1;
    }
    
    const char* path = argv[1];
    
    /* Build absolute path if needed */
    char fullpath[64];
    int i = 0;
    
    if (path[0] != '/') {
        fullpath[i++] = '/';
    }
    
    while (*path && i < 62) {
        fullpath[i++] = *path++;
    }
    fullpath[i] = '\0';
    
    int fd = open(fullpath, O_RDONLY);
    if (fd < 0) {
        puts("cat: cannot open ");
        puts(fullpath);
        return 1;
    }
    
    char buf[256];
    int n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, n);
    }
    
    close(fd);
    return 0;
}
