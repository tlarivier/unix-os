/*
 * exec.c — POSIX execve: copy argv/envp from user, load ELF into a fresh
 * process_memory_t, atomic swap, reset CLOEXEC fds and signal handlers,
 * then jump to ring 3; also exposes setup_exec_user_stack used by the
 * init bringup path.
 *
 * Invariants:
 *  - All kmalloc / kfree happen BEFORE the cli that fences the cr3 swap
 *    and jump_to_usermode; no heap traffic inside that window.
 *  - process_memory_t is committed by atomic swap; on load_elf failure
 *    current->memory is restored to the previous image (re-entrant under
 *    page-fault mid-load).
 *  - signal_handlers[] are reset to SIG_DFL on exec, except SIG_IGN which
 *    POSIX requires to be preserved.
 *  - context.esp / context.ebp must reflect the user stack only on the
 *    success path; partial writes leave the process unrunnable.
 *
 * Not allowed:
 *  - kmalloc / kfree / vfs_* after the cli that precedes the cr3 swap.
 *  - Taking proc_table_lock or any spinlock across jump_to_usermode.
 *  - Mutating process_t->state here (delegated to scheduler/process_exit).
 */

#include <kernel/elf.h>
#include <kernel/elf_loader.h>
#include <kernel/errno.h>
#include <kernel/exec.h>
#include <kernel/gdt.h>
#include <kernel/kernel.h>
#include <kernel/kstring.h>
#include <kernel/memory.h>
#include <kernel/process.h>
#include <kernel/sched_arch.h>
#include <kernel/signal.h>
#include <kernel/uaccess.h>
#include <kernel/vfs.h>
#include <uapi/signal.h>
#include <uapi/syscalls.h>

#define MAX_ARG_LEN 4096
#define MAX_EXEC_ARGS 64
#define MAX_EXEC_ENVS 32

static int copy_to_user_phys(process_memory_t *mem, uint32_t user_addr,
                             const void *src, size_t len) {
  if (!mem || !mem->page_directory)
    return -EINVAL;

  const uint8_t *s = (const uint8_t *)src;

  while (len > 0) {
    uint32_t phys = get_physical_addr(mem->page_directory, user_addr);
    if (!phys)
      return -EFAULT;

    uint32_t frame = phys & 0xFFFFF000;
    uint32_t offset = phys & 0xFFF;
    uint32_t chunk = PAGE_SIZE_CONST - offset;
    if (chunk > len)
      chunk = len;

    uint8_t *dst = (uint8_t *)copy_to_frame(frame, offset, s, chunk);
    if (!dst)
      return -EFAULT;

    user_addr += chunk;
    s += chunk;
    len -= chunk;
  }
  return 0;
}

static void cleanup_exec_args(char **kernel_args, int count) {
  if (!kernel_args)
    return;
  for (int i = 0; i < count; i++)
    kfree(kernel_args[i]);
}

static int copy_exec_args(char *const argv[], char **kernel_argv,
                          int max_args) {
  if (!argv || !kernel_argv)
    return 0;
  int argc = 0;
  for (int i = 0; i < max_args; i++) {
    char *user_ptr = NULL;
    int ret = copy_from_user(&user_ptr, &argv[i], sizeof(char *));
    if (IS_ERROR(ret))
      return ret;

    if (user_ptr == NULL)
      break;

    kernel_argv[i] = kmalloc(MAX_ARG_LEN);
    if (!kernel_argv[i]) {
      cleanup_exec_args(kernel_argv, i);
      return -ENOMEM;
    }
    ret = copy_str_from_user(kernel_argv[i], user_ptr, MAX_ARG_LEN);
    if (IS_ERROR(ret)) {
      kfree(kernel_argv[i]);
      cleanup_exec_args(kernel_argv, i);
      return ret;
    }
    argc++;
  }
  return argc;
}

int setup_exec_user_stack(process_t *proc, char **argv, int argc, char **envp,
                          int envc) {
  if (!proc || !proc->memory)
    return -EINVAL;

  process_memory_t *mem = proc->memory;
  uint32_t stack_top = USER_STACK_BASE - 4096;

  size_t strings_size = 0;
  for (int i = 0; i < argc; i++)
    if (argv[i])
      strings_size += kstrlen(argv[i]) + 1;
  for (int i = 0; i < envc; i++)
    if (envp[i])
      strings_size += kstrlen(envp[i]) + 1;

  int auxv_count =
      (proc->interp_base != 0) ? 14 : 0; /* 7 pairs for dynamic linking */

  uint32_t strings_addr = (stack_top - strings_size) & ~3;
  uint32_t auxv_addr = strings_addr - auxv_count * sizeof(uint32_t);
  uint32_t envp_start = auxv_addr - (envc + 1) * sizeof(uint32_t);
  uint32_t argv_start = envp_start - (argc + 1) * sizeof(uint32_t);
  uint32_t argc_addr = argv_start - sizeof(uint32_t);
  argc_addr &= ~0xF; /* 16-byte align */

  argv_start = argc_addr + sizeof(uint32_t);
  envp_start = argv_start + (argc + 1) * sizeof(uint32_t);
  auxv_addr = envp_start + (envc + 1) * sizeof(uint32_t);
  strings_addr = auxv_addr + auxv_count * sizeof(uint32_t);

  uint32_t val = (uint32_t)argc;
  if (copy_to_user_phys(mem, argc_addr, &val, sizeof(val)) < 0)
    return -EFAULT;

  uint32_t str_ptr = strings_addr;
  for (int i = 0; i < argc; i++) {
    size_t len = kstrlen(argv[i]) + 1;
    if (copy_to_user_phys(mem, str_ptr, argv[i], len) < 0)
      return -EFAULT;
    if (copy_to_user_phys(mem, argv_start + i * sizeof(uint32_t), &str_ptr,
                          sizeof(uint32_t)) < 0)
      return -EFAULT;
    str_ptr += len;
  }
  val = 0;
  if (copy_to_user_phys(mem, argv_start + argc * sizeof(uint32_t), &val,
                        sizeof(val)) < 0)
    return -EFAULT;

  /* Write envp pointers and strings */
  for (int i = 0; i < envc; i++) {
    size_t len = kstrlen(envp[i]) + 1;
    if (copy_to_user_phys(mem, str_ptr, envp[i], len) < 0)
      return -EFAULT;
    if (copy_to_user_phys(mem, envp_start + i * sizeof(uint32_t), &str_ptr,
                          sizeof(uint32_t)) < 0)
      return -EFAULT;
    str_ptr += len;
  }
  val = 0;
  if (copy_to_user_phys(mem, envp_start + envc * sizeof(uint32_t), &val,
                        sizeof(val)) < 0)
    return -EFAULT;

  if (proc->interp_base != 0) {
    uint32_t auxv[14];
    auxv[0] = AT_PHDR;
    auxv[1] = proc->elf_phdr;
    auxv[2] = AT_PHENT;
    auxv[3] = sizeof(Elf32_Phdr);
    auxv[4] = AT_PHNUM;
    auxv[5] = proc->elf_phnum;
    auxv[6] = AT_PAGESZ;
    auxv[7] = PAGE_SIZE_CONST;
    auxv[8] = AT_BASE;
    auxv[9] = proc->interp_base;
    auxv[10] = AT_ENTRY;
    auxv[11] = proc->elf_entry;
    auxv[12] = AT_NULL;
    auxv[13] = 0;
    if (copy_to_user_phys(mem, auxv_addr, auxv, sizeof(auxv)) < 0)
      return -EFAULT;

    proc->context.esp = argc_addr;
    proc->context.ebp = argc_addr;
  } else {
    uint32_t frame_base = argc_addr - 16; /* Space for ret, argc, argv, envp */
    frame_base &= ~0xF;

    val = 0;
    if (copy_to_user_phys(mem, frame_base, &val, sizeof(val)) < 0)
      return -EFAULT;
    val = (uint32_t)argc;
    if (copy_to_user_phys(mem, frame_base + 4, &val, sizeof(val)) < 0)
      return -EFAULT;
    val = argv_start;
    if (copy_to_user_phys(mem, frame_base + 8, &val, sizeof(val)) < 0)
      return -EFAULT;
    val = envp_start;
    if (copy_to_user_phys(mem, frame_base + 12, &val, sizeof(val)) < 0)
      return -EFAULT;

    proc->context.esp = frame_base;
    proc->context.ebp = frame_base;
  }

  return 0;
}

int sys_execve(const char *pathname, char *const argv[], char *const envp[]) {
  if (!pathname)
    return -EINVAL;

  process_t *me = get_current_process();
  if (!me)
    return -ESRCH;

  char *kernel_path = NULL;
  char *kargv[MAX_EXEC_ARGS + 1];
  char *kenvp[MAX_EXEC_ENVS + 1];
  int argc = 0, envc = 0;
  process_memory_t *old_memory = me->memory;
  process_memory_t *new_memory = NULL;
  int ret;

  kmemset(kargv, 0, sizeof(kargv));
  kmemset(kenvp, 0, sizeof(kenvp));

  kernel_path = kmalloc(PATH_MAX_CONST);
  if (!kernel_path)
    return -ENOMEM;

  ret = copy_str_from_user(kernel_path, pathname, PATH_MAX_CONST);
  if (IS_ERROR(ret))
    goto out;
  ret = 0;

  if (kernel_path[0] != '/') {
    ret = -ENOENT;
    goto out;
  }

  argc = copy_exec_args(argv, kargv, MAX_EXEC_ARGS);
  if (argc < 0) {
    ret = argc;
    argc = 0;
    goto out;
  }

  envc = copy_exec_args(envp, kenvp, MAX_EXEC_ENVS);
  if (envc < 0) {
    ret = envc;
    envc = 0;
    goto out;
  }

  {
    struct stat _st;
    if (vfs_stat(kernel_path, &_st) < 0) {
      ret = -ENOENT;
      goto out;
    }
  }

  new_memory = create_process_memory();
  if (!new_memory) {
    ret = -ENOMEM;
    goto out;
  }

  uint32_t saved_cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(saved_cr3));
  __asm__ volatile("mov %0, %%cr3"
                   :
                   : "r"((uint32_t)kernel_page_directory)
                   : "memory");

  ret = load_elf_process_into(kernel_path, me, new_memory);
  if (IS_ERROR(ret)) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(saved_cr3) : "memory");
    destroy_process_memory(new_memory);
    goto out;
  }

  me->memory = new_memory;
  me->user_stack_base = new_memory->stack_base;

  ret = setup_exec_user_stack(me, kargv, argc, kenvp, envc);
  __asm__ volatile("mov %0, %%cr3" : : "r"(saved_cr3) : "memory");
  if (IS_ERROR(ret)) {
    destroy_process_memory(new_memory);
    me->memory = old_memory;
    goto out;
  }

  const char *bn = kernel_path;
  for (int i = 0; kernel_path[i]; i++) {
    if (kernel_path[i] == '/')
      bn = &kernel_path[i + 1];
  }
  kstrncpy(me->name, bn, sizeof(me->name));

  for (int i = 0; i < MAX_OPEN_FILES_CONST; i++) {
    if (me->fd_table[i].of_idx != 0 && (me->fd_table[i].flags & O_CLOEXEC)) {
      vfs_close(i);
    }
  }

  for (int i = 1; i < NSIG_HANDLED; i++) {
    if (me->signal_handlers[i] != SIG_IGN) {
      me->signal_handlers[i] = SIG_DFL;
    }
  }
  me->signal_mask = 0;
  me->signal_pending = 0;

  destroy_process_memory(old_memory);

  cleanup_exec_args(kargv, argc);
  cleanup_exec_args(kenvp, envc);
  kfree(kernel_path);

  __asm__ volatile("cli");
  switch_page_directory(me->memory->page_directory);
  tss_set_kernel_stack((uint32_t)me->kernel_stack + KERNEL_STACK_SIZE);
  jump_to_usermode(me->context.eip, me->context.esp);

  /* unreachable */
  return 0;

out:
  cleanup_exec_args(kargv, argc);
  cleanup_exec_args(kenvp, envc);
  kfree(kernel_path);
  return ret;
}
