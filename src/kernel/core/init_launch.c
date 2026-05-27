/*
 * init_launch.c — One-shot bringup of /sbin/init: build the first
 * process_t, load its ELF, prime the argv/envp stack and transition to
 * ring 3. Called exactly once from kernel_main, before the scheduler is
 * enabled.
 *
 * Invariants:
 *  - Runs strictly before scheduler_enabled is set; the manual RUNNING
 *    stamp and process_switch() bypass are sound only in this window.
 *  - interrupt_stack is the kernel stack used until init_proc->kernel_stack
 *    becomes installable via tss_set_kernel_stack.
 *  - Any failure on this path is unrecoverable: init_bringup_fail() panics.
 *  - Console / termios must be initialised before opening stdio fds.
 *
 * Not allowed:
 *  - Being invoked twice, or after the scheduler is live.
 *  - Reimplementing execve's user-stack layout: defer to
 *    setup_exec_user_stack() in exec.c.
 *  - Holding any spinlock across jump_to_usermode.
 */

#include <kernel/console.h>
#include <kernel/constants.h>
#include <kernel/elf_loader.h>
#include <kernel/errno.h>
#include <kernel/exec.h>
#include <kernel/gdt.h>
#include <kernel/kernel.h>
#include <kernel/paging.h>
#include <kernel/process.h>
#include <kernel/sched_arch.h>
#include <kernel/scheduler.h>
#include <kernel/termios.h>
#include <kernel/vfs.h>
#include <stdint.h>

static uint8_t interrupt_stack[8192] __attribute__((aligned(4096)));

static void init_bringup_fail(const char *reason) {
  KERNEL_PANIC(reason);
  while (1)
    __asm__ volatile("hlt");
}

void start_init_process(void) {
  tss_set_kernel_stack((uint32_t)interrupt_stack + sizeof(interrupt_stack));

  console_init();
  termios_init();

  process_t *init_proc = process_create("init", NULL);
  if (!init_proc)
    init_bringup_fail("init: process_create failed");

  __atomic_store_n(&init_proc->state, PROCESS_RUNNING, __ATOMIC_RELEASE);
  process_switch(init_proc);

  console_open_stdio(init_proc);

  init_proc->memory = create_process_memory();
  if (!init_proc->memory)
    init_bringup_fail("init: create_process_memory failed");
  init_proc->user_stack_base = init_proc->memory->stack_base;

  process_init_canary(init_proc);

  if (IS_ERROR(load_elf_process("/sbin/init", init_proc))) {
    init_bringup_fail("init: load_elf_process failed");
  }

  char *kargv[] = {"init", NULL};
  char *kenvp[] = {NULL};
  if (IS_ERROR(setup_exec_user_stack(init_proc, kargv, 1, kenvp, 0))) {
    init_bringup_fail("init: setup_exec_user_stack failed");
  }

  switch_page_directory(init_proc->memory->page_directory);
  tss_set_kernel_stack((uint32_t)init_proc->kernel_stack + KERNEL_STACK_SIZE);
  jump_to_usermode(init_proc->context.eip, init_proc->context.esp);

  init_bringup_fail("init: returned from jump_to_usermode");
}
