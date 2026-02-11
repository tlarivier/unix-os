#include <sys/types.h>
#include <syscall.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        puts("Usage: rm <file>");
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
    
    int ret = unlink(fullpath);
    if (ret < 0) {
        puts("rm: cannot remove ");
        puts(fullpath);
        return 1;
    }
    
    return 0;
}
