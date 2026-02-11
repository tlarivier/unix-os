#include <sys/types.h>
#include <syscall.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        puts("Usage: touch <file>");
        return 1;
    }
    
    const char* path = argv[1];
    
    int fd = open(path, O_CREAT | O_WRONLY);
    if (fd < 0) {
        puts("touch: cannot create ");
        puts(path);
        return 1;
    }
    
    close(fd);
    return 0;
}
