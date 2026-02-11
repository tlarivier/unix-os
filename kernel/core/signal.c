#include <kernel/process.h>
#include <kernel/memory.h>
#include <kernel/errno.h>
#include <kernel/kernel.h>
#include <kernel/uaccess.h>
#include <../uapi/signal.h>
#include <uapi/sigaction.h>

void process_init_signals(process_t* proc) {
    if (!proc) return;
    proc->signal_mask = 0;
    proc->signal_pending = 0;
    for (int i = 0; i < NSIG && i < 32; i++) {
        proc->signal_handlers[i] = SIG_DFL;
    }
}

int process_send_signal(pid_t pid, int signal) {
    if (signal < 1 || signal >= NSIG || signal >= 32) return -EINVAL;
    process_t* target = process_find_by_pid(pid);
    if (!target) {
        return -ESRCH;
    }
    switch (signal) {
        case SIGKILL:
            target->state = PROCESS_TERMINATED;
            target->exit_code = 128 + SIGKILL;  /* Standard convention */
            return 0;
        case SIGSTOP:
            target->state = PROCESS_BLOCKED;
            return 0;
        case SIGCONT:
            if (target->state == PROCESS_BLOCKED) {
                target->state = PROCESS_READY;
            }
            return 0;
        default:
            if (target->signal_mask & (1U << signal)) {
                target->signal_pending |= (1U << signal);
            } else {
                return process_deliver_signal(target, signal);
            }
            break;
    }
    
    return 0;
}

int process_deliver_signal(process_t* proc, int signal) {
    if (!proc || signal < 1 || signal >= NSIG || signal >= 32) return -EINVAL;
    
    sig_handler_t handler = proc->signal_handlers[signal];
    
    if (handler == SIG_IGN) return 0;
    if (handler == SIG_DFL) {
        switch (signal) {
            case SIGTERM:
            case SIGINT:
            case SIGQUIT:
            case SIGABRT:
                proc->state = PROCESS_TERMINATED;
                proc->exit_code = 128 + signal;
                break;
            case SIGTSTP:
            case SIGTTIN:
            case SIGTTOU:
                /* Job control: stop the process */
                proc->state = PROCESS_BLOCKED;
                proc->stop_signal = signal;
                /* Notify parent with SIGCHLD */
                if (proc->ppid > 0) {
                    process_send_signal(proc->ppid, SIGCHLD);
                }
                break;
            case SIGCONT:
                /* Job control: continue if stopped */
                if (proc->state == PROCESS_BLOCKED && proc->stop_signal != 0) {
                    proc->state = PROCESS_READY;
                    proc->stop_signal = 0;
                }
                break;
            case SIGCHLD:
            case SIGUSR1:
            case SIGUSR2:
                /* Ignore by default */
                break;
            case SIGSEGV:
            case SIGBUS:
            case SIGFPE:
            case SIGILL:
                proc->state = PROCESS_TERMINATED;
                proc->exit_code = 128 + signal;
                break;
            default:
                proc->state = PROCESS_TERMINATED;
                proc->exit_code = 128 + signal;
                break;
        }
    } else {
        /* Custom signal handler - mark as pending for delivery at context switch */
        proc->signal_pending |= (1U << signal);
        proc->current_signal = signal;
    }
    
    return 0;
}

int process_set_signal_handler(process_t* proc, int signal, sig_handler_t handler) {
    if (!proc || signal < 1 || signal >= NSIG) return -EINVAL;
    if (signal == SIGKILL || signal == SIGSTOP) return -EINVAL;
    
    sig_handler_t old_handler = proc->signal_handlers[signal];
    proc->signal_handlers[signal] = handler;
    
    return (int)(uintptr_t)old_handler;
}

void process_kill_children(process_t* parent) {
    if (!parent) return;
    for (int i = 0; i < MAX_PROCESSES_CONST; i++) {
        process_t* proc = process_get_by_index(i);
        if (proc && proc->ppid == parent->pid && proc->state != PROCESS_TERMINATED) {
            kprintf("Sending SIGTERM to child process PID %d\n", proc->pid);
            process_send_signal(proc->pid, SIGTERM);
        }
    }
}

typedef struct signal_frame {
    uint32_t sigreturn_addr;  
    uint32_t signal;          
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp;
    uint32_t eip;            
    uint32_t esp;            
    uint32_t eflags;
} signal_frame_t;

static const uint8_t sigreturn_trampoline[] = {
    0xB8, 0x77, 0x00, 0x00, 0x00,  
    0xCD, 0x80                     
};

static void setup_signal_frame(process_t* proc, int signal) {
    sig_handler_t handler = proc->signal_handlers[signal];
    if (handler == SIG_DFL || handler == SIG_IGN) return;
    
    uint32_t old_esp = proc->context.esp;
    
    uint32_t trampoline_addr = old_esp - sizeof(sigreturn_trampoline);
    trampoline_addr &= ~3;  
    
    uint32_t frame_addr = trampoline_addr - sizeof(signal_frame_t);
    frame_addr &= ~3;
    
    if (copy_to_user((void*)trampoline_addr, sigreturn_trampoline, sizeof(sigreturn_trampoline)) < 0) {
        return;
    }
    
    signal_frame_t kframe;
    kframe.sigreturn_addr = trampoline_addr;  /* Handler returns here */
    kframe.signal = (uint32_t)signal;
    kframe.eax    = proc->context.eax;
    kframe.ebx    = proc->context.ebx;
    kframe.ecx    = proc->context.ecx;
    kframe.edx    = proc->context.edx;
    kframe.esi    = proc->context.esi;
    kframe.edi    = proc->context.edi;
    kframe.ebp    = proc->context.ebp;
    kframe.eip    = proc->context.eip;
    kframe.esp    = old_esp;
    kframe.eflags = proc->context.eflags;
    
    if (copy_to_user((void*)frame_addr, &kframe, sizeof(kframe)) < 0) {
        return;
    }
    
    proc->context.esp = frame_addr;
    proc->context.eip = (uint32_t)handler;
    
    proc->current_signal = 0;
}

int sys_sigreturn(void) {
    process_t* proc = get_current_process();
    if (!proc) return -ESRCH;
    
    signal_frame_t frame;
    if (copy_from_user(&frame, (void*)proc->context.esp, sizeof(frame)) < 0) {
        return -EFAULT;
    }
    
    if (frame.eip >= 0xC0000000) {
        return -EFAULT;
    }
    
    if (frame.esp >= 0xC0000000 || frame.esp < 0x1000) {
        return -EFAULT;
    }
    
    frame.eflags &= 0x000000FF;  /* Keep only CF, PF, AF, ZF, SF */
    frame.eflags |= 0x00000200;  /* Ensure IF (interrupts) enabled */
    
    proc->context.eax = frame.eax;
    proc->context.ebx = frame.ebx;
    proc->context.ecx = frame.ecx;
    proc->context.edx = frame.edx;
    proc->context.esi = frame.esi;
    proc->context.edi = frame.edi;
    proc->context.ebp = frame.ebp;
    proc->context.eip = frame.eip;
    proc->context.esp = frame.esp;
    proc->context.eflags = frame.eflags;
    
    return 0;
}

void process_check_signals(process_t* proc) {
    if (!proc || proc->state == PROCESS_TERMINATED) return;
    
    sigset_t deliverable = proc->signal_pending & ~proc->signal_mask;
    if (deliverable == 0) return;
    
    for (int sig = 1; sig < NSIG; sig++) {
        if (deliverable & (1U << sig)) {
            proc->signal_pending &= ~(1U << sig);
            
            sig_handler_t handler = proc->signal_handlers[sig];
            if (handler != SIG_DFL && handler != SIG_IGN) {
                setup_signal_frame(proc, sig);
            } else {
                process_deliver_signal(proc, sig);
            }
            break;
        }
    }
}

int32_t sys_sigaction(uint32_t signum, uint32_t act_ptr, uint32_t oldact_ptr, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    process_t *proc = get_current_process();
    if (!proc) return -ESRCH;
    
    int sig = (int)signum;
    if (sig < 1 || sig >= NSIG) return -EINVAL;
    if (sig == SIGKILL || sig == SIGSTOP) return -EINVAL;
    
    if (oldact_ptr) {
        struct sigaction old;
        old.sa_handler = proc->signal_handlers[sig];
        old.sa_mask = proc->signal_mask;
        old.sa_flags = 0;
        old.sa_restorer = 0;
        
        int rc = copy_to_user((void*)oldact_ptr, &old, sizeof(struct sigaction));
        if (IS_ERROR(rc)) return rc;
    }
    
    if (act_ptr) {
        struct sigaction act;
        int rc = copy_from_user(&act, (void*)act_ptr, sizeof(struct sigaction));
        if (IS_ERROR(rc)) return rc;
        
        proc->signal_handlers[sig] = act.sa_handler;
    }
    
    return 0;
}

int32_t sys_sigprocmask(uint32_t how, uint32_t set_ptr, uint32_t oldset_ptr, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    process_t *proc = get_current_process();
    if (!proc) return -ESRCH;
    
    if (oldset_ptr) {
        int rc = copy_to_user((void*)oldset_ptr, &proc->signal_mask, sizeof(sigset_t));
        if (IS_ERROR(rc)) return rc;
    }
    
    if (set_ptr) {
        sigset_t set;
        int rc = copy_from_user(&set, (void*)set_ptr, sizeof(sigset_t));
        if (IS_ERROR(rc)) return rc;
        
        set &= ~((1U << SIGKILL) | (1U << SIGSTOP));
        
        switch (how) {
            case SIG_BLOCK:
                proc->signal_mask |= set;
                break;
            case SIG_UNBLOCK:
                proc->signal_mask &= ~set;
                break;
            case SIG_SETMASK:
                proc->signal_mask = set;
                break;
            default:
                return -EINVAL;
        }
    }
    
    return 0;
}

int32_t sys_sigsuspend(uint32_t mask_ptr, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    
    process_t *proc = get_current_process();
    if (!proc) return -ESRCH;
    
    sigset_t old_mask = proc->signal_mask;
    
    if (mask_ptr) {
        sigset_t new_mask;
        int rc = copy_from_user(&new_mask, (void*)mask_ptr, sizeof(sigset_t));
        if (IS_ERROR(rc)) return rc;
        
        new_mask &= ~((1U << SIGKILL) | (1U << SIGSTOP));
        proc->signal_mask = new_mask;
    }
    
    while (!(proc->signal_pending & ~proc->signal_mask)) {
        proc->state = PROCESS_BLOCKED;
        yield();  
    }
    proc->state = PROCESS_RUNNING;
    
    proc->signal_mask = old_mask;
    
    return -EINTR;  
}

int32_t sys_sigpending(uint32_t set_ptr, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    
    process_t *proc = get_current_process();
    if (!proc) return -ESRCH;
    
    if (!set_ptr) return -EINVAL;
    
    int rc = copy_to_user((void*)set_ptr, &proc->signal_pending, sizeof(sigset_t));
    if (IS_ERROR(rc)) return rc;
    
    return 0;
}

int signal_pending_check(void) {
    process_t* proc = get_current_process();
    if (!proc) return 0;
    return (proc->signal_pending & ~proc->signal_mask) != 0;
}
