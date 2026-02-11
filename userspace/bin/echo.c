#include <sys/types.h>
#include <syscall.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        puts(argv[i]);
        if (i < argc - 1) {
            write(1, " ", 1);
        }
    }
    write(1, "\n", 1);
    return 0;
}
