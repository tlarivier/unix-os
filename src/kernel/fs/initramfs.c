/*
 * initramfs.c — stages the baked-in CPIO archive into ramfs at boot. The
 * legacy ext2-to-ramfs mirror was retired in wave 3 once vfs_fd / vfs_path
 * began dispatching directly through the fs_ops vtable; /mnt is now a
 * live ext2 mount and never copied into ramfs.
 *
 * Invariants:
 *  - Runs only during early bringup, before user processes consume the FS.
 *  - VFS calls (vfs_open/write/close/mkdir) take vfs_lock internally; this
 *    file must not pre-acquire vfs_lock or any fs lock.
 *  - create_parent_dirs is shared with cpio_parse.c (non-static intentionally).
 *
 * Not allowed:
 *  - Calling schedule() while holding any FS lock.
 *  - Running after init userspace is live (one-shot boot path).
 *  - Re-introducing an ext2-to-ramfs mirror — /mnt must reach disk via
 *    the VFS-mount dispatcher.
 */
#include <kernel/initramfs.h>
#include <kernel/kprintf.h>
#include <kernel/memory.h>
#include <kernel/vfs.h>
#include <stdint.h>

void create_parent_dirs(const char *path) {
  char dir[256];
  int i = 0;

  while (path[i] && i < 255) {
    dir[i] = path[i];
    if (path[i] == '/' && i > 0) {
      dir[i] = '\0';
      vfs_mkdir(dir, 0755);
      dir[i] = '/';
    }
    i++;
  }
}

int install_userspace_binaries(void) {
  vfs_mkdir("/bin", 0755);
  vfs_mkdir("/sbin", 0755);
  vfs_mkdir("/lib", 0755);

  kprintf("Loading from initramfs...\n");
  int count = initramfs_init();
  if (count > 0) {
    kprintf("Loaded %d files\n", count);
  }
  return count;
}
