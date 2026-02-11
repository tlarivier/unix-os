#ifndef KERNEL_EXEC_H
#define KERNEL_EXEC_H

int sys_execve(const char *pathname, char *const argv[], char *const envp[]);

#endif 
