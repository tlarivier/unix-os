#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <kernel/hashtable.h>
#include <kernel/waitq.h>

#include <kernel/constants.h>
#include <kernel/paging.h>
#include <kernel/process_mm.h>
#include <kernel/types.h>
#include <kernel/vfs.h>
#include <stddef.h>
#include <stdint.h>
#include <uapi/resource.h>
#include <uapi/signal.h>

#define PROCESS_READY 0
#define PROCESS_RUNNING 1
#define PROCESS_BLOCKED 2
#define PROCESS_TERMINATED 3
#define PROCESS_ZOMBIE 4

#define MAX_PRIORITY 7
#define MIN_PRIORITY 0
#define DEFAULT_PRIORITY 4
#define MAX_PROCESS_NAME 16

struct process;
typedef struct process process_t;

typedef struct {
  uint32_t of_idx;
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
  pid_t pgid;
  pid_t sid;
  uid_t uid;
  gid_t gid;
  uid_t euid;
  gid_t egid;
  uid_t suid;
  gid_t sgid;
  volatile uint32_t state;
  uint32_t priority;
  int8_t mlfq_queue;
  char name[MAX_PROCESS_NAME];
  process_context_t context;
  process_memory_t *memory;
  void *kernel_stack;
  uint32_t user_stack_base;
  uint32_t canary_expected;
  int first_sched;
  int exit_code;
  int tty;
  uint32_t umask;
  sigset_t signal_mask;
  sigset_t signal_pending;
#ifndef NSIG_HANDLED
#define NSIG_HANDLED 32
#endif
  sig_handler_t signal_handlers[NSIG_HANDLED];
  struct process *next;
  uint32_t time_slice;
  char cwd[256];
  proc_fd_t fd_table[MAX_OPEN_FILES_CONST];
  uint32_t open_files_count;
  uint32_t alarm_ticks;
  uint32_t tls_base;
  uint32_t clear_child_tid;
  uint32_t elf_entry;
  uint32_t elf_phdr;
  uint32_t elf_phnum;
  uint32_t interp_base;
  wait_queue_t children_wq;
  uint32_t owner_cpu;
  hash_entry_t ht_node;
} process_t;

void process_init(void);
process_t *process_create(const char *name, void *entry_point);
void process_terminate(process_t *proc);
process_t *get_current_process(void);
process_t *process_find_by_pid(uint32_t pid);
void process_switch(process_t *next);
void process_exit(int exit_code);
void process_init_canary(process_t *p);
void process_check_current_canary(void);
process_t *process_find_child(uint32_t ppid, int32_t pid, int must_be_zombie);
process_t *process_get_by_index(uint32_t index);
void process_init_priority(process_t *proc);
void scheduler_remove_process(process_t *proc);
int signal_pending_check(void);
void check_stack_canary(void);
extern void asm_context_switch(process_context_t *old_ctx,
                               process_context_t *new_ctx);
extern pid_t console_pgrp;

#endif
