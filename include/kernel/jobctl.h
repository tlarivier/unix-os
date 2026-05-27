#ifndef KERNEL_JOBCTL_H
#define KERNEL_JOBCTL_H

#include <kernel/types.h>

int process_setpgid(pid_t pid, pid_t pgid);
pid_t process_getpgid(pid_t pid);
pid_t process_getpgrp(void);
pid_t process_setsid(void);
pid_t process_getsid(pid_t pid);
pid_t process_tcgetpgrp(int fd);
int process_tcsetpgrp(int fd, pid_t pgrp);

#endif
