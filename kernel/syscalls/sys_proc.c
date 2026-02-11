#include "syscall.h"
#include <kernel/scheduler.h>
#include <../uapi/signal.h>

int32_t sys_exit(uint32_t status, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    
    extern void kprintf(const char*, ...);
    process_t* cur = get_current_process();
    kprintf("[EXIT] pid=%d status=%d\n", cur ? cur->pid : -1, status);
    
    process_exit((int)status);
    schedule();
    
    __asm__ volatile("cli");
    while (1) { __asm__ volatile("hlt"); }
    return 0;
}

int32_t sys_getpid(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u1; (void)u2; (void)u3; (void)u4; (void)u5;
    
    process_t* cur = get_current_process();
    return cur ? cur->pid : 0;
}

int32_t sys_getuid(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u1; (void)u2; (void)u3; (void)u4; (void)u5;
    process_t* cur = get_current_process();
    return cur ? (int32_t)cur->uid : 0;
}

int32_t sys_getgid(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u1; (void)u2; (void)u3; (void)u4; (void)u5;
    process_t* cur = get_current_process();
    return cur ? (int32_t)cur->gid : 0;
}

int32_t sys_geteuid(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u1; (void)u2; (void)u3; (void)u4; (void)u5;
    process_t* cur = get_current_process();
    return cur ? (int32_t)cur->euid : 0;
}

int32_t sys_getegid(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u1; (void)u2; (void)u3; (void)u4; (void)u5;
    process_t* cur = get_current_process();
    return cur ? (int32_t)cur->egid : 0;
}

int32_t sys_setuid(uint32_t uid, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    process_t* cur = get_current_process();
    if (!cur) return -ESRCH;
    
    if (cur->euid == 0) {
        cur->uid = cur->euid = cur->suid = (uid_t)uid;
        return 0;
    }
    if (uid == cur->uid || uid == cur->euid || uid == cur->suid) {
        cur->euid = (uid_t)uid;
        return 0;
    }
    return -EPERM;
}

int32_t sys_setgid(uint32_t gid, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    process_t* cur = get_current_process();
    if (!cur) return -ESRCH;
    
    if (cur->euid == 0) {
        cur->gid = cur->egid = cur->sgid = (gid_t)gid;
        return 0;
    }
    if (gid == cur->gid || gid == cur->egid || gid == cur->sgid) {
        cur->egid = (gid_t)gid;
        return 0;
    }
    return -EPERM;
}

int32_t sys_seteuid(uint32_t euid, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    process_t* cur = get_current_process();
    if (!cur) return -ESRCH;
    
    if (cur->euid == 0 || euid == cur->uid || euid == cur->suid) {
        cur->euid = (uid_t)euid;
        return 0;
    }
    return -EPERM;
}

int32_t sys_setegid(uint32_t egid, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    process_t* cur = get_current_process();
    if (!cur) return -ESRCH;
    
    if (cur->euid == 0 || egid == cur->gid || egid == cur->sgid) {
        cur->egid = (gid_t)egid;
        return 0;
    }
    return -EPERM;
}

int32_t sys_waitpid(uint32_t pid, uint32_t status_ptr, uint32_t opts, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    process_t* cur = get_current_process();
    if (!cur) return -1;
    
    process_t* any_child = process_find_child(cur->pid, (int32_t)pid, 0);
    if (!any_child) return -ECHILD;
    
    if (opts & 1) {
        process_t* child = process_find_child(cur->pid, (int32_t)pid, 1);
        if (!child) return 0;
        
        if (status_ptr) {
            int status = child->exit_code;
            int rc = copy_to_user((void*)status_ptr, &status, sizeof(status));
            if (IS_ERROR(rc)) return rc;
        }
        int cpid = child->pid;
        process_reap(child);
        return cpid;
    }
    
    /* Block until a child becomes zombie */
    process_t* child = NULL;
    while (!(child = process_find_child(cur->pid, (int32_t)pid, 1))) {
        any_child = process_find_child(cur->pid, (int32_t)pid, 0);
        if (!any_child) return -ECHILD;
        yield();
    }
    
    if (status_ptr) {
        int status = child->exit_code;
        int rc = copy_to_user((void*)status_ptr, &status, sizeof(status));
        if (IS_ERROR(rc)) return rc;
    }
    
    int cpid = child->pid;
    process_reap(child);
    return cpid;
}

int32_t sys_kill(uint32_t pid, uint32_t sig, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    if (sig >= NSIG) return -EINVAL;
    return process_send_signal((pid_t)pid, (int)sig);
}

int32_t sys_signal(uint32_t sig, uint32_t handler, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    if (sig >= NSIG || sig == 0) return -EINVAL;
    
    process_t* cur = get_current_process();
    if (!cur) return -ESRCH;
    
    sig_handler_t h = (sig_handler_t)(uintptr_t)handler;
    return process_set_signal_handler(cur, (int)sig, h);
}

int32_t sys_gettid(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u1; (void)u2; (void)u3; (void)u4; (void)u5;
    process_t* cur = get_current_process();
    return cur ? cur->pid : -ESRCH;  /* TID == PID for now */
}

int32_t sys_set_tid_address(uint32_t tidptr, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)tidptr; (void)u2; (void)u3; (void)u4; (void)u5;
    process_t* cur = get_current_process();
    if (!cur) return -ESRCH;
    return cur->pid;
}
