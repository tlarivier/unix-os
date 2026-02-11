#include <sys/types.h>
#include <syscall.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        puts("Usage: mkdir <path>");
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
    
    int ret = mkdir(fullpath, 0755);
    if (ret < 0) {
        puts("mkdir: failed");
        return 1;
    }
    
    puts("Created: ");
    puts(fullpath);
    return 0;
}
