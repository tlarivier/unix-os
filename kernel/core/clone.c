#include <kernel/process.h>
#include <kernel/memory.h>
#include <kernel/paging.h>
#include <kernel/errno.h>
#include <kernel/scheduler.h>
#include <kernel/uaccess.h>
#include <stdint.h>

#define CLONE_VM             0x00000100 /* Share virtual memory */
#define CLONE_FS             0x00000200 /* Share filesystem info */
#define CLONE_FILES          0x00000400 /* Share file descriptors */
#define CLONE_SIGHAND        0x00000800 /* Share signal handlers */
#define CLONE_THREAD         0x00010000 /* Same thread group */
#define CLONE_PARENT         0x00008000 /* Same parent as caller */
#define CLONE_CHILD_SETTID   0x01000000 /* Set TID in child */
#define CLONE_PARENT_SETTID  0x00100000 /* Set TID in parent */
#define CLONE_CHILD_CLEARTID 0x00200000 /* Clear TID on exit */
#define CLONE_SETTLS         0x00080000 /* Set TLS for child */

extern process_t* current_process;

extern pid_t allocate_next_pid(void);

int32_t sys_clone(uint32_t flags, uint32_t child_stack, uint32_t ptid, uint32_t tls, uint32_t ctid) {
    process_t* parent = get_current_process();
    if (!parent) return -ESRCH;
    
    process_t* child = kmalloc(sizeof(process_t));
    if (!child) return -ENOMEM;
    
    child->pid        = allocate_next_pid();
    child->ppid       = (flags & CLONE_PARENT) ? parent->ppid : parent->pid;
    child->pgid       = parent->pgid;
    child->sid        = parent->sid;
    child->state      = PROCESS_READY;
    child->priority   = parent->priority;
    child->time_slice = parent->time_slice;
    
    for (int i = 0; i < MAX_PROCESS_NAME; i++) {
        child->name[i] = parent->name[i];
    }
    
    child->uid  = parent->uid;
    child->gid  = parent->gid;
    child->euid = parent->euid;
    child->egid = parent->egid;
    child->suid = parent->suid;
    child->sgid = parent->sgid;
    
    /* Handle memory sharing */
    if (flags & CLONE_VM) {
        child->memory = parent->memory;
        child->memory->refcount++;
    } else {
        child->memory = clone_process_memory(parent->memory);
        if (!child->memory) {
            kfree(child);
            return -ENOMEM;
        }
    }
    
    /* Allocate kernel stack */
    child->kernel_stack = kmalloc(4096);
    if (!child->kernel_stack) {
        if (!(flags & CLONE_VM) && child->memory) {
            /* Free cloned memory to prevent leak */
            extern void destroy_process_memory(process_memory_t* memory);
            destroy_process_memory(child->memory);
        }
        kfree(child);
        return -ENOMEM;
    }
    
    if (child_stack != 0) {
        child->user_stack_base = child_stack;
    } else {
        child->user_stack_base = parent->user_stack_base;
    }
    
    child->context = parent->context;
    child->context.eax = 0;  /* Child returns 0 */
    
    /* Set child stack pointer if provided */
    if (child_stack != 0) {
        child->context.esp = child_stack;
    }
    
    /* Handle file descriptor sharing 
     * Note: True sharing would require a shared fd_table structure with refcount.
     * For now, we copy and increment VFS refcounts for open files. */
    for (int i = 0; i < MAX_OPEN_FILES_CONST; i++) {
        child->fd_table[i] = parent->fd_table[i];
        if (child->fd_table[i].node_idx != 0 && child->fd_table[i].refcount > 0) {
            extern void vfs_node_incref(int node_idx);
            vfs_node_incref((int)child->fd_table[i].node_idx);
        }
    }
    
    /* Handle signal handler sharing 
     * Note: True sharing would require a shared sighand structure with refcount.
     * For now, we copy handlers (which is correct for fork-like behavior). */
    for (int i = 0; i < NSIG; i++) {
        child->signal_handlers[i] = parent->signal_handlers[i];
    }
    
    child->signal_mask    = parent->signal_mask;
    child->signal_pending = 0;
    child->current_signal = 0;
    child->stop_signal    = 0;
    
    /* Copy filesystem info */
    if (flags & CLONE_FS) {
        /* Share cwd, umask */
        for (int i = 0; i < 256; i++) {
            child->cwd[i] = parent->cwd[i];
        }
        child->umask = parent->umask;
    } else {
        for (int i = 0; i < 256; i++) {
            child->cwd[i] = parent->cwd[i];
        }
        child->umask = parent->umask;
    }
    
    child->tty         = parent->tty;
    child->first_sched = 1;
    child->exit_code   = 0;
    child->alarm_ticks = 0;
    child->next        = NULL;
    
    if (flags & CLONE_SETTLS) {
        /* Store TLS base address in process structure
         * On x86, this will be loaded into GS segment base on context switch
         * For now, store the full address - actual GDT setup would be in context switch */
        child->tls_base = tls;
    } else {
        child->tls_base = 0;
    }
    
    child->clear_child_tid = 0;
    if (flags & CLONE_CHILD_CLEARTID) {
        child->clear_child_tid = ctid;
    }
    
    if ((flags & CLONE_PARENT_SETTID) && ptid != 0) {
        int tid = child->pid;
        copy_to_user((void*)ptid, &tid, sizeof(int));
    }
    
    if ((flags & CLONE_CHILD_SETTID) && ctid != 0) {
        /* This would be set in child's address space */
        /* For CLONE_VM, it's the same space */
        if (flags & CLONE_VM) {
            int tid = child->pid;
            copy_to_user((void*)ctid, &tid, sizeof(int));
        }
    }
    
    scheduler_add_process(child);
    
    return child->pid;
}
