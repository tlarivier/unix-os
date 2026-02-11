#include <sys/types.h>
#include <syscall.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    char cwd[64];
    if (getcwd(cwd, sizeof(cwd)) >= 0) {
        puts(cwd);
    } else {
        puts("/");
    }
    return 0;
}
