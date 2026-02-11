#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel.h>
#include <kernel/process.h>
#include <kernel/memory.h>
#include <kernel/debug.h>
#include <kernel/random.h>
#include <kernel/errno.h>
#include <kernel/constants.h>
#include <kernel/gdt.h>
#include <kernel/hashtable.h>
#include <kernel/kstring.h>
#include <kernel/vfs.h>

static hash_table_t process_table_ht;   /* PID -> process */
static process_t* process_table[MAX_PROCESSES_CONST];
extern process_t* current_process;
static uint32_t next_pid = 1;
static process_t kernel_proc;

pid_t allocate_next_pid(void) {
    return (pid_t)next_pid++;
}

void process_init(void) {
    hash_table_init(&process_table_ht, "processes");
    for (int i = 0; i < MAX_PROCESSES_CONST; i++) {
        process_table[i] = NULL;
    }
    kernel_proc.pid      = 0;
    kernel_proc.state    = PROCESS_RUNNING;
    kernel_proc.priority = 1;
    kernel_proc.pgid     = 0;
    kernel_proc.sid      = 0;
    kernel_proc.tty      = -1;
    kernel_proc.umask    = 022;
    kstrncpy(kernel_proc.name, "kernel", sizeof(kernel_proc.name));
    
    kernel_proc.kernel_stack = kmalloc(KERNEL_STACK_SIZE);
    if (kernel_proc.kernel_stack) {
        kernel_proc.context.esp = (uint32_t)kernel_proc.kernel_stack + KERNEL_STACK_SIZE;
    }
    kernel_proc.context.eflags = 0x202;
    kernel_proc.memory = NULL;
    
    process_table[0] = &kernel_proc;
    hash_table_insert(&process_table_ht, 0, &kernel_proc);
    current_process = &kernel_proc;
}

process_t* process_create(const char* name, void* entry_point) {
    if (!name) return NULL;
    
    int slot = -1;
    for (int i = 1; i < MAX_PROCESSES_CONST; i++) {
        if (process_table[i] == NULL) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return NULL;
    process_t* proc = (process_t*)kmalloc(sizeof(process_t));
    if (!proc) return NULL;
    memset(proc, 0, sizeof(process_t));  /* Zero all fields including fd_table */
    process_table[slot] = proc;
    proc->pid      = next_pid++;
    proc->ppid     = current_process ? current_process->pid : 0;
    proc->state    = PROCESS_READY;
    proc->priority = 4;
    proc->uid = proc->gid = 0;
    kstrncpy(proc->name, name, sizeof(proc->name));
    proc->context.eip = (uint32_t)entry_point;
    proc->context.eflags = 0x202;
    proc->context.eax    = proc->context.ebx = 0;
    proc->context.ecx    = proc->context.edx = 0;
    proc->context.esi    = proc->context.edi = proc->context.ebp = 0;
    proc->kernel_stack   = kmalloc(KERNEL_STACK_SIZE);
    if (!proc->kernel_stack) {
        process_table[slot] = NULL;
        kfree(proc);
        return NULL;
    }
    proc->context.esp = (uint32_t)proc->kernel_stack + KERNEL_STACK_SIZE;
    if (entry_point) {
        proc->memory = create_process_memory();
    } else {
        proc->memory = NULL;
    }
    process_init_signals(proc);
    process_init_limits(proc);
    process_init_priority(proc);
    
    proc->pgid  = proc->pid;
    proc->sid   = current_process ? current_process->sid : proc->pid;  /* Inherit session or create new */
    proc->tty   = current_process ? current_process->tty : -1;  /* Inherit controlling terminal */
    proc->umask = current_process ? current_process->umask : 022;  /* Inherit umask or default */
    
    if (current_process && current_process->cwd[0]) {
        kstrncpy(proc->cwd, current_process->cwd, sizeof(proc->cwd));
    } else {
        proc->cwd[0] = '/';
        proc->cwd[1] = '\0';
    }
    
    hash_table_insert(&process_table_ht, proc->pid, proc);
    
    return proc;
}

void process_terminate(process_t* proc) {
    if (!proc || proc->pid == 0) return;
    
    proc->state = PROCESS_TERMINATED;
    
    if (proc->kernel_stack) {
        kfree(proc->kernel_stack);
        proc->kernel_stack = NULL;
    }
    
    if (proc->memory) {
        destroy_process_memory(proc->memory);
        proc->memory = NULL;
    }
    hash_table_remove(&process_table_ht, proc->pid);
    for (int i = 1; i < MAX_PROCESSES_CONST; i++) {
        if (process_table[i] == proc) {
            process_table[i] = NULL;
            kfree(proc);
            break;
        }
    }
}

void process_exit(int exit_code) {
    if (!current_process || current_process->pid == 0) return;
    
    current_process->exit_code = exit_code;
    current_process->state = PROCESS_ZOMBIE;
    if (current_process->ppid) {
        process_send_signal(current_process->ppid, SIGCHLD);
    }
    
    scheduler_remove_process(current_process);
}

process_t* get_current_process(void) {
    return current_process ? current_process : &kernel_proc;
}

void process_switch(process_t* next) {
    if (next) current_process = next;
}

process_t* process_find_by_pid(uint32_t pid) {
    process_t* proc = (process_t*)hash_table_lookup(&process_table_ht, pid);
    if (proc && proc->state != PROCESS_TERMINATED) return proc;
    return NULL;
}

process_t* process_get_by_index(uint32_t idx) {
    if (idx >= MAX_PROCESSES_CONST) return NULL;
    if (!process_table[idx]) return NULL;
    if (process_table[idx]->state == PROCESS_TERMINATED) return NULL;
    return process_table[idx];
}

process_t* process_find_child(uint32_t ppid, int32_t pid, int zombie_only) {
    if (pid > 0) {
        process_t* p = (process_t*)hash_table_lookup(&process_table_ht, (uint32_t)pid);
        if (p && p->ppid == (pid_t)ppid && p->state != PROCESS_TERMINATED) {
            if (!zombie_only || p->state == PROCESS_ZOMBIE) return p;
        }
        return NULL;
    }
    for (int i = 0; i < MAX_PROCESSES_CONST; i++) {
        process_t* p = process_table[i];
        if (p && p->ppid == (pid_t)ppid && p->state != PROCESS_TERMINATED) {
            if (!zombie_only || p->state == PROCESS_ZOMBIE) return p;
        }
    }
    return NULL;
}

void process_reap(process_t* child) {
    if (!child || child->state != PROCESS_ZOMBIE) return;
    process_terminate(child);  /* Now properly frees memory */
}

extern void asm_context_switch(process_context_t* old_ctx, process_context_t* new_ctx);
extern void jump_to_usermode(uint32_t entry, uint32_t stack);

void context_switch(process_t* old, process_t* new) {
    if (!old || !new || old == new) return;
    if (old->state == PROCESS_RUNNING) old->state = PROCESS_READY;
    new->state = PROCESS_RUNNING;
    current_process = new;
    
    if (new->memory && new->memory->page_directory) {
        switch_page_directory(new->memory->page_directory);
    }
    if (new->kernel_stack) {
        tss_set_kernel_stack((uint32_t)new->kernel_stack + KERNEL_STACK_SIZE);
    }
    
    if (new->first_sched) {
        new->first_sched = 0;
    }
    
    if (old->state == PROCESS_ZOMBIE || old->state == PROCESS_TERMINATED) {
        asm_context_switch(NULL, &new->context);
    } else {
        asm_context_switch(&old->context, &new->context);
    }
}

void process_init_canary(process_t* p) {
    if (!p || !p->kernel_stack) return;
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    uint32_t canary = STACK_CANARY_PROCESS_MAGIC ^ p->pid ^ lo;
    
    p->canary_expected = canary;
    *((volatile uint32_t*)p->kernel_stack) = canary;
}

void process_check_current_canary(void) {
    return;
#if 0
    process_t* p = get_current_process();
    if (!p || !p->kernel_stack) return;
    
    uint32_t val = *((volatile uint32_t*)p->kernel_stack);
    if (val != p->canary_expected) {
        kprintf("[PANIC] Stack corruption PID %d\n", p->pid);
        while(1) __asm__ volatile("hlt");
    }
#endif
}

void process_close_all_fds(process_t* p) {
    if (!p) return;
    
    for (int i = 0; i < MAX_OPEN_FILES_CONST; i++) {
        if (p->fd_table[i].node_idx != 0 || p->fd_table[i].flags != 0) {
            vfs_close(i);  /* Close via VFS */
            p->fd_table[i].node_idx = 0;
            p->fd_table[i].offset = 0;
            p->fd_table[i].flags = 0;
            p->fd_table[i].refcount = 0;
        }
    }
    p->open_files_count = 0;
}

void process_init_process_signals(process_t* p) { process_init_signals(p); }
process_t* process_lookup(uint32_t pid) { return process_find_by_pid(pid); }
int process_get_list(char* buf, size_t size) { if (buf && size) buf[0] = '\0'; return 0; }

void process_display_info(void) {
    kprintf("Processes: ");
    for (int i = 0; i < MAX_PROCESSES_CONST; i++) {
        if (process_table[i] && process_table[i]->state != PROCESS_TERMINATED) {
            kprintf("[%d:%s] ", process_table[i]->pid, process_table[i]->name);
        }
    }
    kprintf("\n");
}

process_t* process_create_user(const char* name, const char* path) {
    (void)path;
    return process_create(name, NULL);
}

static void fork_cleanup(process_t* child, int slot) {
    if (child->memory) destroy_process_memory(child->memory);
    if (child->kernel_stack) kfree(child->kernel_stack);
    if (slot >= 0) process_table[slot] = NULL;
    kfree(child);
}

int32_t sys_fork(void* regs_ptr) {
    process_t* parent = get_current_process();
    if (!parent) return -ESRCH;
    
    /* PID 0 (kernel_proc) should never fork - it has no userspace context */
    if (parent->pid == 0) {
        kprintf("FORK: kernel (PID 0) cannot fork!\n");
        return -EPERM;
    }
    
    typedef struct {
        uint32_t ds;
        uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
        uint32_t int_no, err_code;
        uint32_t eip, cs, eflags, useresp, ss;
    } regs_t;
    regs_t* regs = (regs_t*)regs_ptr;
    
    int slot = -1;
    for (int i = 1; i < MAX_PROCESSES_CONST; i++) {
        if (process_table[i] == NULL) { slot = i; break; }
    }
    if (slot < 0) return -EAGAIN;
    
    process_t* child = (process_t*)kmalloc(sizeof(process_t));
    if (!child) return -ENOMEM;
    
    memset(child, 0, sizeof(process_t));
    
    child->pid         = next_pid++;
    child->ppid        = parent->pid;
    child->uid         = parent->uid;
    child->gid         = parent->gid;
    child->state       = PROCESS_READY;
    child->priority    = parent->priority;
    child->time_slice  = parent->time_slice;
    child->signal_mask = parent->signal_mask;
    child->umask       = parent->umask;
    
    for (int i = 0; i < MAX_PROCESS_NAME && parent->name[i]; i++)
        child->name[i] = parent->name[i];
    for (int i = 0; i < 256 && parent->cwd[i]; i++)
        child->cwd[i] = parent->cwd[i];
    
    if (!parent->kernel_stack) {
        kprintf("FORK ERROR: parent PID %d has no kernel_stack!\n", parent->pid);
        fork_cleanup(child, -1);
        return -EINVAL;
    }
    
    child->kernel_stack = kmalloc(KERNEL_STACK_SIZE);
    if (!child->kernel_stack) {
        fork_cleanup(child, -1);
        return -ENOMEM;
    }
    memcpy(child->kernel_stack, parent->kernel_stack, KERNEL_STACK_SIZE);
    
    /* Initialize canary for child */
    process_init_canary(child);
    
    /* 
     * Set up child to return from fork with eax=0.
     * 
     * The child's kernel stack is a copy of parent's, which has the syscall
     * frame (registers + iret frame). We need to:
     * 1. Modify the eax in the syscall frame to 0 (fork returns 0 in child)
     * 2. Set child's context to point to fork_child_return which does iret
     * 3. Set child's context.esp to top of child's kernel stack
     */
    if (regs) {
        uint32_t parent_stack_base = (uint32_t)parent->kernel_stack;
        uint32_t parent_stack_top = parent_stack_base + KERNEL_STACK_SIZE;
        uint32_t regs_addr = (uint32_t)regs;
        
        if (regs_addr < parent_stack_base || regs_addr >= parent_stack_top) {
            kprintf("FORK: regs %x not in stack [%x-%x]\n", regs_addr, parent_stack_base, parent_stack_top);
            fork_cleanup(child, -1);
            return -EINVAL;
        }
        
        uint32_t regs_offset = regs_addr - parent_stack_base;
        regs_t* child_regs = (regs_t*)((uint32_t)child->kernel_stack + regs_offset);
        
        
        child_regs->eax = 0;
        
        child_regs->cs = 0x1B;   /* User code segment (ring 3) */
        child_regs->ss = 0x23;   /* User data segment (ring 3) */
        child_regs->ds = 0x23;   /* User data segment */
        
        extern void fork_child_return(void);
        child->context.eip = (uint32_t)fork_child_return;
        child->context.esp = (uint32_t)child_regs;  /* Point to syscall frame */
        child->context.ebp = 0;
        child->context.eax = 0;
        child->context.ebx = child->context.ecx = child->context.edx = 0;
        child->context.esi = child->context.edi = 0;
        child->context.eflags = 0x202;  
        child->context.cs = 0x08;  
        child->context.ds = child->context.es = child->context.fs = child->context.gs = child->context.ss = 0x10;
    } else {
        child->context = parent->context;
        child->context.eax = 0;
    }
    
    child->first_sched = 1;
    
    if (parent->memory) {
        child->memory = clone_process_memory(parent->memory);
        if (!child->memory) {
            fork_cleanup(child, -1);
            return -ENOMEM;
        }
    }
    
    for (int i = 0; i < MAX_OPEN_FILES_CONST; i++) {
        child->fd_table[i] = parent->fd_table[i];
        if (parent->fd_table[i].node_idx != 0 || parent->fd_table[i].flags != 0) {
            parent->fd_table[i].refcount++;
            child->fd_table[i].refcount = 1;
            vfs_node_incref(parent->fd_table[i].node_idx);
        }
    }
    
    /* Inherit signal handlers from parent  */
    for (int i = 0; i < NSIG; i++)
        child->signal_handlers[i] = parent->signal_handlers[i];
    
    child->first_sched = 1;
    
    child->next = NULL;
    process_table[slot] = child;
    hash_table_insert(&process_table_ht, child->pid, child);
    
    scheduler_add_process(child);
    
    return child->pid;
}
