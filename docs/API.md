# UnixOS Kernel API Reference

## Driver Interfaces

### Timer (`kernel/timer.h`)
```c
void timer_init(uint32_t frequency);  // Initialize PIT
void timer_handle_irq(void);          // Called from IRQ0
uint32_t get_timer_ticks(void);       // Get tick count
uint32_t get_seconds(void);           // Get seconds since boot
void sleep_ms(uint32_t ms);           // Sleep milliseconds
```

### Keyboard (`kernel/keyboard.h`)
```c
void keyboard_init(void);             // Initialize driver
void keyboard_handle_irq(void);       // Called from IRQ1
int kb_has_char(void);                // Check buffer
char kb_get_char(void);               // Blocking read
char kb_try_get_char(void);           // Non-blocking read
```

### VGA Text (`kernel/vga.h`)
```c
void vga_init(void);                  // Initialize VGA
void vga_clear(void);                 // Clear screen
void vga_putchar(char c);             // Print character
void vga_print(const char *str);      // Print string
void vga_print_at(const char *str, int x, int y, uint8_t attr);
```

### VGA Graphics (`kernel/vga_graphics.h`)
```c
int vga_set_mode_13h(void);           // Switch to 320x200x256
void vga_set_text_mode(void);         // Switch to text mode
void vga_putpixel(int x, int y, uint8_t color);
void vga_fill_rect(int x, int y, int w, int h, uint8_t color);
```

---

## Core Interfaces

### I/O Ports (`kernel/io.h`)
```c
static inline void outb(uint16_t port, uint8_t value);
static inline uint8_t inb(uint16_t port);
static inline void outw(uint16_t port, uint16_t value);
static inline uint16_t inw(uint16_t port);
static inline void outl(uint16_t port, uint32_t value);
static inline uint32_t inl(uint16_t port);
static inline void io_wait(void);
```

### Printf (`kernel/kprintf.h`)
```c
void kprintf(const char *format, ...);  // %s %d %u %x %p %%
void itoa(uint32_t num, char *buffer, int base);
```

---

## VFS Interface (`kernel/vfs.h`)

### File Operations
```c
int32_t vfs_open(const char *path, int flags, mode_t mode);
void vfs_close(int fd);
ssize_t vfs_read(int fd, void *buf, size_t count);
ssize_t vfs_write(int fd, const void *buf, size_t count);
off_t vfs_lseek(int fd, off_t offset, int whence);
int vfs_stat(const char *path, struct stat *st);
int vfs_fstat(int fd, struct stat *st);
int vfs_dup(int oldfd);
int vfs_dup2(int oldfd, int newfd);
```

### Directory Operations
```c
int32_t vfs_mkdir(const char *path, mode_t mode);
ssize_t vfs_readdir_fd(int fd, void *buffer, size_t size);
```

---

## Process Interface (`kernel/process.h`)

```c
void process_init(void);
process_t *process_create(const char *name, void *entry);
void process_exit(int status);
process_t *get_current_process(void);
```

---

## Scheduler Interface (`kernel/scheduler.h`)

```c
void scheduler_init(void);
void scheduler_enable(void);
void scheduler_add_process(process_t *proc);
void schedule(void);
void yield(void);
```

---

## Memory Interface (`kernel/memory.h`)

```c
void memory_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void *krealloc(void *ptr, size_t size);
```

---

## Adding New Drivers (Nouveau système)

### Méthode 1: Via device.h abstraction
```c
#include <kernel/device.h>

static struct device_ops my_ops = {
    .init  = my_init,
    .read  = my_read,
    .write = my_write
};

static struct device my_device = {
    .name = "mydev",
    .type = DEV_CHAR,
    .ops  = &my_ops
};

// Dans init:
device_register(&my_device);
```

### Méthode 2: Direct (legacy)
1. Create header in `include/kernel/<driver>.h`
2. Create source in `kernel/drivers/<category>/<driver>.c`
3. Add to `KERNEL_DRIVER_SOURCES` in Makefile
4. Add init call in `kernel/init/main.c`

### Pour IRQ-based drivers:
```c
#include <kernel/irq.h>

void my_irq_handler(uint32_t irq, void *data) {
    // Handle interrupt
}

// Dans init:
irq_register(IRQ5, my_irq_handler, NULL, "mydriver");
```

---

## IPC Interface

### Pipes (`kernel/ipc/pipe.c`)
```c
int32_t sys_pipe(uint32_t pipefd[2]);  // Create pipe
// Read/write via standard VFS operations
```

### System V Semaphores (`kernel/ipc/sem.c`)
```c
int32_t sys_semget(key_t key, int nsems, int semflg);
int32_t sys_semop(int semid, struct sembuf *sops, size_t nsops);
int32_t sys_semctl(int semid, int semnum, int cmd, ...);
```

### System V Shared Memory (`kernel/ipc/shm.c`)
```c
int32_t sys_shmget(key_t key, size_t size, int shmflg);
void*   sys_shmat(int shmid, const void *shmaddr, int shmflg);
int32_t sys_shmdt(const void *shmaddr);
int32_t sys_shmctl(int shmid, int cmd, struct shmid_ds *buf);
```

### Futex (`kernel/ipc/futex.c`)
```c
int32_t sys_futex(uint32_t *uaddr, int op, uint32_t val,
                  const struct timespec *timeout, uint32_t *uaddr2);
// op: FUTEX_WAIT, FUTEX_WAKE
// Supports timeout with -ETIMEDOUT
```

---

## Thread Interface (`kernel/core/clone.c`)

```c
int32_t sys_clone(uint32_t flags, void *child_stack,
                  int *ptid, uint32_t tls, int *ctid);

// Clone flags:
#define CLONE_VM        0x00000100  // Share virtual memory
#define CLONE_FS        0x00000200  // Share filesystem info
#define CLONE_FILES     0x00000400  // Share file descriptors
#define CLONE_SIGHAND   0x00000800  // Share signal handlers
#define CLONE_THREAD    0x00010000  // Same thread group
#define CLONE_SETTLS    0x00080000  // Set TLS for child
#define CLONE_CHILD_SETTID   0x01000000
#define CLONE_PARENT_SETTID  0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000
```

---

## Socket Interface (`kernel/net/socket.c`)

```c
int32_t sys_socket(int domain, int type, int protocol);
int32_t sys_bind(int sockfd, const struct sockaddr *addr, socklen_t len);
int32_t sys_listen(int sockfd, int backlog);
int32_t sys_accept(int sockfd, struct sockaddr *addr, socklen_t *len);
int32_t sys_connect(int sockfd, const struct sockaddr *addr, socklen_t len);
int32_t sys_send(int sockfd, const void *buf, size_t len, int flags);
int32_t sys_recv(int sockfd, void *buf, size_t len, int flags);
int32_t sys_shutdown(int sockfd, int how);

// Supported: AF_UNIX domain only
```

---

## FAT12 Filesystem (`kernel/fs/fat12.c`)

```c
int fat12_mount(void *device);           // Mount FAT12 filesystem
int fat12_unmount(void);                 // Unmount
int fat12_lookup(const char *name, uint16_t dir_cluster, fat12_dirent_t *result);
ssize_t fat12_read(uint16_t cluster, uint8_t *buf, size_t count, uint32_t offset);
int fat12_readdir(uint16_t dir_cluster, int index, fat12_dirent_t *result);
int fat12_is_mounted(void);

// Read-only - write operations return -EROFS
```

---

## Adding New Syscalls

1. Define syscall number in `uapi/syscalls.def`
2. Add handler in appropriate `kernel/syscalls/sys_*.c`
3. Add extern declaration in `syscall_table.c`
4. Register in `syscall_handlers[]` in `syscall_table.c`

---

## Constants

All magic numbers should be defined in:
- `include/kernel/constants.h` - System constants
- `include/kernel/ports.h` - Hardware port addresses
- `include/kernel/types.h` - Type definitions
