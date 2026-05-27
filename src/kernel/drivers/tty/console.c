/*
 * console.c — wires fd 0/1/2 of the initial process to the sentinel console
 * inode (CONSOLE_INODE_MAGIC = 0xC0C0) and provides a nominal console_init
 * boot step.
 *
 * Invariants:
 *  - console_init is called exactly once during boot, after vga_init and
 *    keyboard_init, and before usermode_start creates pid 1.
 *  - console_open_stdio populates proc->fd_table[0..2] with the console
 *    sentinel inode and standard O_RDONLY/O_WRONLY flags; refcount starts at 1.
 *  - CONSOLE_INODE_MAGIC is the single sentinel value recognised by pipe.c,
 *    vfs_fd.c, and sys_fs.c to dispatch I/O to the keyboard/vga drivers.
 *
 * Not allowed:
 *  - Calling vfs_* from this TU (/dev is created by kernel/init/main.c).
 *  - Allocating fd_table entries dynamically; the table is a fixed struct
 * field.
 *  - Calling schedule, signal_*, or holding the process table lock.
 */

#include <../uapi/syscalls.h>
#include <kernel/console.h>
#include <kernel/errno.h>
#include <kernel/kprintf.h>
#include <kernel/process.h>

int console_init(void) {
  kprintf("Console device initialized\n");
  return 0;
}

int console_open_stdio(process_t *proc) {
  if (!proc)
    return -EINVAL;

  proc->fd_table[0].of_idx = CONSOLE_INODE_MAGIC;
  proc->fd_table[0].flags = O_RDONLY;
  proc->fd_table[0].offset = 0;
  proc->fd_table[0].refcount = 1;

  proc->fd_table[1].of_idx = CONSOLE_INODE_MAGIC;
  proc->fd_table[1].flags = O_WRONLY;
  proc->fd_table[1].offset = 0;
  proc->fd_table[1].refcount = 1;

  proc->fd_table[2].of_idx = CONSOLE_INODE_MAGIC;
  proc->fd_table[2].flags = O_WRONLY;
  proc->fd_table[2].offset = 0;
  proc->fd_table[2].refcount = 1;

  proc->open_files_count = 3;
  proc->tty = 0;

  return 0;
}
