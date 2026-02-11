#include <sys/types.h>
#include <syscall.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    puts("UnixOS Init (PID 1)");
    
    while (1) {
        int pid = fork();
        
        if (pid < 0) {
            puts("init: fork failed");
            _exit(1);
        }
        
        if (pid == 0) {
            /* Child - exec shell */
            write(1, "CHILD: before execve\n", 21);
            char* args[] = {"/bin/sh", (char*)0};
            execve("/bin/sh", args, (char* const*)0);
            write(1, "CHILD: execve failed!\n", 22);
            _exit(127);
        }
        
        int status = 0;
        waitpid(pid, &status, 0);
        puts("init: shell exited, restarting...");
    }
    
    return 0;
}
