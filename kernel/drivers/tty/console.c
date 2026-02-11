#include <kernel/vga.h>
#include <kernel/keyboard.h>
#include <kernel/vfs.h>
#include <kernel/memory.h>
#include <kernel/errno.h>
#include <kernel/process.h>
#include <kernel/kprintf.h>
#include <../uapi/syscalls.h>

#define CONSOLE_INODE_MAGIC  0xC0C0

/* Console device state */
static int console_initialized = 0;

int console_init(void) {
    if (console_initialized) return 0;
    
    int rc = vfs_mkdir("/dev", 0755);
    if (rc < 0 && rc != -EEXIST) {
        kprintf("console: failed to create /dev\n");
        return rc;
    }
    
    console_initialized = 1;
    kprintf("Console device initialized\n");
    return 0;
}

int console_open_stdio(process_t *proc) {
    if (!proc) return -EINVAL;
    
    proc->fd_table[0].node_idx = CONSOLE_INODE_MAGIC;
    proc->fd_table[0].flags    = O_RDONLY;
    proc->fd_table[0].offset   = 0;
    proc->fd_table[0].refcount = 1;
    
    proc->fd_table[1].node_idx = CONSOLE_INODE_MAGIC;
    proc->fd_table[1].flags    = O_WRONLY;
    proc->fd_table[1].offset   = 0;
    proc->fd_table[1].refcount = 1;
    
    proc->fd_table[2].node_idx = CONSOLE_INODE_MAGIC;
    proc->fd_table[2].flags    = O_WRONLY;
    proc->fd_table[2].offset   = 0;
    proc->fd_table[2].refcount = 1;
    
    proc->open_files_count = 3;
    proc->tty = 0;  /* Console is tty 0 */
    
    return 0;
}

int is_console_fd(int fd) {
    if (fd < 0 || fd > 2) return 0;
    
    process_t *proc = get_current_process();
    if (!proc) return 0;
    
    return (proc->fd_table[fd].node_idx == CONSOLE_INODE_MAGIC);
}
