#include <kernel/process.h>
#include <kernel/memory.h>
#include <kernel/errno.h>
#include <kernel/kernel.h>
#include <kernel/uaccess.h>
#include <../uapi/signal.h>

int32_t sys_setpgid(uint32_t pid, uint32_t pgid, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    process_t *current = get_current_process();
    if (!current) return -ESRCH;
    
    if (pid == 0) pid = current->pid;
    
    if (pgid == 0) pgid = pid;
    
    process_t *target = process_find_by_pid(pid);
    if (!target) return -ESRCH;
    
    if (target->pid != current->pid && target->ppid != current->pid) {
        return -EPERM;
    }
    
    target->pgid = pgid;
    
    return 0;
}

int32_t sys_getpgid(uint32_t pid, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    
    process_t *current = get_current_process();
    if (!current) return -ESRCH;
    
    if (pid == 0) {
        return current->pgid;
    }
    
    process_t *target = process_find_by_pid(pid);
    if (!target) return -ESRCH;
    
    return target->pgid;
}

int32_t sys_getpgrp(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u1; (void)u2; (void)u3; (void)u4; (void)u5;
    
    process_t *current = get_current_process();
    if (!current) return -ESRCH;
    
    return current->pgid;
}

int32_t sys_setsid(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u1; (void)u2; (void)u3; (void)u4; (void)u5;
    
    process_t *current = get_current_process();
    if (!current) return -ESRCH;
    
    if (current->pid == current->sid) {
        return -EPERM;
    }
    
    current->sid  = current->pid;
    current->pgid = current->pid;
    current->tty  = -1;
    
    return current->sid;
}

int32_t sys_getsid(uint32_t pid, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    
    process_t *current = get_current_process();
    if (!current) return -ESRCH;
    
    if (pid == 0) {
        return current->sid;
    }
    
    process_t *target = process_find_by_pid(pid);
    if (!target) return -ESRCH;
    
    return target->sid;
}

int32_t sys_tcgetpgrp(uint32_t fd, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    
    if (fd > 2) return -ENOTTY;
    
    extern pid_t console_pgrp;
    return console_pgrp;
}

int32_t sys_tcsetpgrp(uint32_t fd, uint32_t pgrp, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    process_t *current = get_current_process();
    if (!current) return -ESRCH;
    
    if (fd > 2) return -ENOTTY;
    
    extern pid_t console_pgrp;
    console_pgrp = (pid_t)pgrp;
    
    return 0;
}

pid_t console_pgrp = 1;
