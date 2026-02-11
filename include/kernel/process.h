#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/types.h>
#include <kernel/constants.h>
#include <kernel/paging.h>
#include <kernel/vfs.h>
#include <../uapi/resource.h>
#include <../uapi/signal.h>

#define PROCESS_READY       0
#define PROCESS_RUNNING     1
#define PROCESS_BLOCKED     2
#define PROCESS_TERMINATED  3
#define PROCESS_ZOMBIE      4

#define MAX_PRIORITY        7
#define MIN_PRIORITY        0
#define DEFAULT_PRIORITY    4
#define MAX_PROCESS_NAME    16

struct process;
typedef struct process process_t;

/* Per-process file descriptor entry */
typedef struct {
    uint32_t node_idx;
    uint32_t offset;
    int flags;
    int refcount;
} proc_fd_t;

typedef struct process_context {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eip, eflags;
    uint16_t cs, ds, es, fs, gs, ss;
} process_context_t;

typedef struct process {
    pid_t pid;
    pid_t ppid;
    pid_t pgid;                 /* Process group ID */
    pid_t sid;                  /* Session ID */
    uid_t uid;                  /* Real user ID */
    gid_t gid;                  /* Real group ID */
    uid_t euid;                 /* Effective user ID */
    gid_t egid;                 /* Effective group ID */
    uid_t suid;                 /* Saved user ID */
    gid_t sgid;                 /* Saved group ID */
    uint32_t state;
    uint32_t priority;
    char name[MAX_PROCESS_NAME];
    process_context_t context;
    process_memory_t* memory;
    void* kernel_stack;
    uint32_t user_stack_base;
    uint32_t canary_expected;
    int first_sched;            /* 1 = needs iret to userspace on first schedule */
    int exit_code;
    int tty;                    /* Controlling terminal (-1 if none) */
    uint32_t umask;             /* File creation mask */
    sigset_t signal_mask;
    sigset_t signal_pending;
    int current_signal;
    int stop_signal;            /* Signal that stopped this process (for job control) */
    sig_handler_t signal_handlers[NSIG];
    struct process* next;
    uint32_t time_slice;
    char cwd[256];
    proc_fd_t fd_table[MAX_OPEN_FILES_CONST];
    uint32_t open_files_count;
    uint32_t alarm_ticks;       /* Timer ticks until SIGALRM */
    uint32_t tls_base;          /* Thread Local Storage base address */
    uint32_t clear_child_tid;   /* Address to clear on thread exit (CLONE_CHILD_CLEARTID) */
    /* Dynamic linking info (PT_INTERP) */
    uint32_t elf_entry;         /* Original program entry point */
    uint32_t elf_phdr;          /* Program headers address */
    uint32_t elf_phnum;         /* Number of program headers */
    uint32_t interp_base;       /* Interpreter base address */
} process_t;

void process_init(void);
process_t* process_create(const char* name, void* entry_point);
void process_terminate(process_t* proc);
process_t* get_current_process(void);
void scheduler_remove_process(process_t* proc);
void yield(void);
process_t* process_find_by_pid(uint32_t pid);
void process_switch(process_t* next);
void process_exit(int exit_code);
process_memory_t* process_memory_create(void);
void process_memory_destroy(process_memory_t* mem);
void process_init_canary(process_t* p);
void process_check_current_canary(void);
process_t* process_find_child(uint32_t ppid, int32_t pid, int must_be_zombie);
void process_reap(process_t* child);
void process_init_signals(process_t* proc);
int process_send_signal(pid_t pid, int signal);
int process_deliver_signal(process_t* proc, int signal);
int process_set_signal_handler(process_t* proc, int signal, sig_handler_t handler);
void process_kill_children(process_t* parent);
void process_close_all_fds(process_t* proc);
process_t* process_get_by_index(uint32_t index);
int sys_execve(const char *pathname, char *const argv[], char *const envp[]);
void process_init_limits(process_t *proc);
int sys_getrlimit(int resource, struct rlimit *rlim);
int sys_setrlimit(int resource, const struct rlimit *rlim);
int sys_nice(int increment);
void process_init_priority(process_t *proc);

#endif
