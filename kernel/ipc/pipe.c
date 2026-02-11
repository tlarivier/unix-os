#include <kernel/process.h>
#include <kernel/memory.h>
#include <kernel/errno.h>
#include <kernel/uaccess.h>
#include <kernel/kernel.h>
#include <kernel/hashtable.h>
#include <kernel/pipe.h>
#include <../uapi/ipc.h>
#include <../uapi/syscalls.h>
#include <kernel/types.h>
#include <kernel/constants.h>

#define PIPE_READ  0x01
#define PIPE_WRITE 0x02

typedef struct pipe_registry {
    hash_table_t pipe_table;
    uint32_t next_pipe_id;
    uint32_t active_pipes;
} pipe_registry_t;

static pipe_registry_t pipe_registry;

static slub_cache_t *pipe_buffer_cache = NULL;
static slub_cache_t *pipe_temp_cache = NULL;

static pipe_buffer_t *pipe_create_buffer(size_t size) {
    if (size > PIPE_MAX_SIZE) {
        size = PIPE_MAX_SIZE;
    }
    if (size < PIPE_BUF) {
        size = PIPE_BUF;
    }
    
    pipe_buffer_t *pipe = kmalloc(sizeof(pipe_buffer_t));
    if (!pipe) {
        return NULL;
    }
    
    pipe->data = kmalloc(size);
    if (!pipe->data) {
        kfree(pipe);
        return NULL;
    }
    
    pipe->size = size;
    pipe->read_pos = 0;
    pipe->write_pos = 0;
    pipe->count = 0;
    pipe->readers = 0;
    pipe->writers = 0;
    pipe->flags = 0;
    spinlock_init(&pipe->lock, "pipe");
    
    return pipe;
}

static void pipe_destroy_buffer(pipe_buffer_t *pipe) {
    if (!pipe) return;
    
    if (pipe->data) {
        kfree(pipe->data);
    }
    kfree(pipe);
}

static ssize_t pipe_read_buffer(pipe_buffer_t *pipe, char *buf, size_t count) {
    if (!pipe || !buf) return -EINVAL;
    
    spin_lock(&pipe->lock);
    
    if (pipe->writers == 0 && pipe->count == 0) {
        spin_unlock(&pipe->lock);
        return 0;
    }
    
    /* If buffer empty, block or return EAGAIN */
    while (pipe->count == 0) {
        if (pipe->writers == 0) {
            spin_unlock(&pipe->lock);
            return 0;  /* EOF - no more writers */
        }
        if (pipe->flags & O_NONBLOCK) {
            spin_unlock(&pipe->lock);
            return -EAGAIN;
        }
        /* Block: release lock, check signals, yield, reacquire */
        spin_unlock(&pipe->lock);
        extern void yield(void);
        extern int signal_pending_check(void);
        if (signal_pending_check()) {
            return -EINTR;  /* Interrupted by signal */
        }
        yield();
        spin_lock(&pipe->lock);
    }
    
    size_t to_read = (count < pipe->count) ? count : pipe->count;
    size_t bytes_read = 0;
    
    while (bytes_read < to_read && pipe->count > 0) {
        size_t chunk = pipe->size - pipe->read_pos;
        if (chunk > (to_read - bytes_read)) {
            chunk = to_read - bytes_read;
        }
        if (chunk > pipe->count) {
            chunk = pipe->count;
        }
        
        /* Copy data */
        for (size_t i = 0; i < chunk; i++) {
            buf[bytes_read + i] = pipe->data[pipe->read_pos + i];
        }
        
        bytes_read += chunk;
        pipe->read_pos = (pipe->read_pos + chunk) % pipe->size;
        pipe->count -= chunk;
    }
    
    spin_unlock(&pipe->lock);
    return (ssize_t)bytes_read;
}

static ssize_t pipe_write_buffer(pipe_buffer_t *pipe, const char *buf, size_t count) {
    if (!pipe || !buf) return -EINVAL;
    
    spin_lock(&pipe->lock);
    
    if (pipe->readers == 0) {
        spin_unlock(&pipe->lock);
        process_t* cur = get_current_process();
        if (cur) {
            process_send_signal(cur->pid, 13);  
        }
        return -EPIPE;
    }
    
    size_t available = pipe->size - pipe->count;
    if (available == 0) {
        spin_unlock(&pipe->lock);
        if (pipe->flags & O_NONBLOCK) {
            return -EAGAIN;
        }
        return -EAGAIN;
    }
    
    size_t to_write = (count < available) ? count : available;
    size_t bytes_written = 0;
    
    while (bytes_written < to_write && pipe->count < pipe->size) {
        size_t chunk = pipe->size - pipe->write_pos;
        if (chunk > (to_write - bytes_written)) {
            chunk = to_write - bytes_written;
        }
        if (chunk > (pipe->size - pipe->count)) {
            chunk = pipe->size - pipe->count;
        }
        
        for (size_t i = 0; i < chunk; i++) {
            pipe->data[pipe->write_pos + i] = buf[bytes_written + i];
        }
        
        bytes_written += chunk;
        pipe->write_pos = (pipe->write_pos + chunk) % pipe->size;
        pipe->count += chunk;
    }
    
    spin_unlock(&pipe->lock);
    return (ssize_t)bytes_written;
}

static void pipe_registry_init(void) {
    hash_table_init(&pipe_registry.pipe_table, "pipes");
    pipe_registry.next_pipe_id = 1;
    pipe_registry.active_pipes = 0;
    pipe_buffer_cache = slub_cache_create("pipe_buffers", sizeof(pipe_buffer_t));
    pipe_temp_cache = slub_cache_create("pipe_temp_4k", 4096);
}

static int pipe_allocate_id(pipe_buffer_t *pipe) {
    uint32_t pipe_id = pipe_registry.next_pipe_id++;
    int ret = hash_table_insert(&pipe_registry.pipe_table, pipe_id, pipe);
    if (ret == 0) {
        pipe_registry.active_pipes++;
        return pipe_id;
    }
    return -1;
}

int sys_pipe(int pipefd[2]) {
    if (!pipefd) return -EINVAL;
    
    int kernel_fds[2] = {-1, -1};
    
    pipe_buffer_t *pipe = pipe_create_buffer(PIPE_BUF);
    if (!pipe) {
        return -ENOMEM;
    }
    
    int pipe_id = pipe_allocate_id(pipe);
    if (pipe_id < 0) {
        pipe_destroy_buffer(pipe);
        return -EMFILE;
    }
    
    process_t *proc = get_current_process();
    if (!proc) {
        hash_table_remove(&pipe_registry.pipe_table, pipe_id);
        pipe_registry.active_pipes--;
        pipe_destroy_buffer(pipe);
        return -ESRCH;
    }
    
    int found = 0;
    for (int i = 3; i < MAX_OPEN_FILES_CONST && found < 2; i++) {
        if (proc->fd_table[i].node_idx == 0 && proc->fd_table[i].flags == 0) {
            kernel_fds[found] = i;
            proc->fd_table[i].node_idx = PIPE_FD_BASE + pipe_id;
            proc->fd_table[i].flags    = (found == 0) ? PIPE_READ : PIPE_WRITE;
            proc->fd_table[i].offset   = 0;
            proc->fd_table[i].refcount = 1;
            found++;
        }
    }
    
    if (found < 2) {
        for (int i = 0; i < found; i++) {
            proc->fd_table[kernel_fds[i]].node_idx = 0;
            proc->fd_table[kernel_fds[i]].flags = 0;
        }
        hash_table_remove(&pipe_registry.pipe_table, pipe_id);
        pipe_registry.active_pipes--;
        pipe_destroy_buffer(pipe);
        return -EMFILE;
    }
    
    pipe->readers = 1;
    pipe->writers = 1;
    
    int ret = copy_to_user(pipefd, kernel_fds, sizeof(kernel_fds));
    if (IS_ERROR(ret)) {
        proc->fd_table[kernel_fds[0]].node_idx = 0;
        proc->fd_table[kernel_fds[0]].flags = 0;
        proc->fd_table[kernel_fds[1]].node_idx = 0;
        proc->fd_table[kernel_fds[1]].flags = 0;
        hash_table_remove(&pipe_registry.pipe_table, pipe_id);
        pipe_registry.active_pipes--;
        pipe_destroy_buffer(pipe);
        return ret;
    }
    
    return 0;
}

int sys_pipe2(int pipefd[2], int flags) {
    if (!pipefd) return -EINVAL;
    
    if (flags & ~(O_CLOEXEC | O_NONBLOCK)) {
        return -EINVAL;
    }
    
    int ret = sys_pipe(pipefd);
    if (IS_ERROR(ret)) {
        return ret;
    }
    
    int kernel_fds[2];
    ret = copy_from_user(kernel_fds, pipefd, sizeof(kernel_fds));
    if (IS_ERROR(ret)) {
        return ret;
    }
    
    process_t *proc = get_current_process();
    if (proc && kernel_fds[0] < MAX_OPEN_FILES_CONST) {
        int pipe_id = proc->fd_table[kernel_fds[0]].node_idx - PIPE_FD_BASE;
        pipe_buffer_t *pipe = hash_table_lookup(&pipe_registry.pipe_table, pipe_id);
        if (pipe) {
            pipe->flags |= flags;
        }
    }
    
    return 0;
}

ssize_t pipe_read(int fd, void *buf, size_t count) {
    if (!buf) return -EINVAL;
    
    if (fd < PIPE_FD_BASE) return -EBADF;
    int pipe_id = (fd - PIPE_FD_BASE) / 2;
    int fd_type = (fd - PIPE_FD_BASE) % 2;
    
    pipe_buffer_t *pipe = hash_table_lookup(&pipe_registry.pipe_table, pipe_id);
    if (!pipe) {
        return -EBADF;
    }
    
    if (fd_type != 0) {  
        return -EBADF;
    }
    
    char *kernel_buf;
    if (count <= 4096 && pipe_temp_cache) {
        kernel_buf = slub_alloc(pipe_temp_cache);
    } else {
        kernel_buf = kmalloc(count);  
    }
    if (!kernel_buf) {
        return -ENOMEM;
    }
    
    ssize_t bytes_read = pipe_read_buffer(pipe, kernel_buf, count);
    
    if (bytes_read > 0) {
        int ret = copy_to_user(buf, kernel_buf, bytes_read);
        if (IS_ERROR(ret)) {
            if (count <= 4096 && pipe_temp_cache) {
                slub_free(pipe_temp_cache, kernel_buf);
            } else {
                kfree(kernel_buf);
            }
            return ret;
        }
    }
    
    if (count <= 4096 && pipe_temp_cache) {
        slub_free(pipe_temp_cache, kernel_buf);
    } else {
        kfree(kernel_buf);
    }
    return bytes_read;
}

ssize_t pipe_write(int fd, const void *buf, size_t count) {
    if (!buf) return -EINVAL;
    
    if (fd < PIPE_FD_BASE) return -EBADF;
    int pipe_id = (fd - PIPE_FD_BASE) / 2;
    int fd_type = (fd - PIPE_FD_BASE) % 2;  /* 0 = read, 1 = write */
    
    pipe_buffer_t *pipe = hash_table_lookup(&pipe_registry.pipe_table, pipe_id);
    if (!pipe) {
        return -EBADF;
    }
    
    if (fd_type != 1) {  /* Not a write FD */
        return -EBADF;
    }
    
    char *kernel_buf;
    if (count <= 4096 && pipe_temp_cache) {
        kernel_buf = slub_alloc(pipe_temp_cache);
    } else {
        kernel_buf = kmalloc(count); 
    }
    if (!kernel_buf) {
        return -ENOMEM;
    }
    
    int ret = copy_from_user(kernel_buf, buf, count);
    if (IS_ERROR(ret)) {
        if (count <= 4096 && pipe_temp_cache) {
            slub_free(pipe_temp_cache, kernel_buf);
        } else {
            kfree(kernel_buf);
        }
        return ret;
    }
    
    ssize_t bytes_written = pipe_write_buffer(pipe, kernel_buf, count);
    
    if (count <= 4096 && pipe_temp_cache) {
        slub_free(pipe_temp_cache, kernel_buf);
    } else {
        kfree(kernel_buf);
    }
    return bytes_written;
}

int pipe_close(int fd) {
    if (fd < PIPE_FD_BASE) return -EBADF;
    int pipe_id = (fd - PIPE_FD_BASE) / 2;
    int fd_type = (fd - PIPE_FD_BASE) % 2;  
    
    pipe_buffer_t *pipe = hash_table_lookup(&pipe_registry.pipe_table, pipe_id);
    if (!pipe) {
        return -EBADF;
    }
    
    if (fd_type == 0) {
        pipe->readers--;
    } else {
        pipe->writers--;
    }
    
    if (pipe->readers == 0 && pipe->writers == 0) {
        hash_table_remove(&pipe_registry.pipe_table, pipe_id);
        pipe_registry.active_pipes--;
        pipe_destroy_buffer(pipe);
    }
    
    return 0;
}

ssize_t pipe_read_by_id(int pipe_id, void *buf, size_t count) {
    if (!buf) return -EINVAL;
    
    pipe_buffer_t *pipe = hash_table_lookup(&pipe_registry.pipe_table, pipe_id);
    if (!pipe) return -EBADF;
    
    char *kernel_buf;
    if (count <= 4096 && pipe_temp_cache) {
        kernel_buf = slub_alloc(pipe_temp_cache);
    } else {
        kernel_buf = kmalloc(count);
    }
    if (!kernel_buf) return -ENOMEM;
    
    ssize_t bytes_read = pipe_read_buffer(pipe, kernel_buf, count);
    
    if (bytes_read > 0) {
        int ret = copy_to_user(buf, kernel_buf, bytes_read);
        if (IS_ERROR(ret)) {
            if (count <= 4096 && pipe_temp_cache) slub_free(pipe_temp_cache, kernel_buf);
            else kfree(kernel_buf);
            return ret;
        }
    }
    
    if (count <= 4096 && pipe_temp_cache) slub_free(pipe_temp_cache, kernel_buf);
    else kfree(kernel_buf);
    return bytes_read;
}

ssize_t pipe_write_by_id(int pipe_id, const void *buf, size_t count) {
    if (!buf) return -EINVAL;
    
    pipe_buffer_t *pipe = hash_table_lookup(&pipe_registry.pipe_table, pipe_id);
    if (!pipe) return -EBADF;
    
    char *kernel_buf;
    if (count <= 4096 && pipe_temp_cache) {
        kernel_buf = slub_alloc(pipe_temp_cache);
    } else {
        kernel_buf = kmalloc(count);
    }
    if (!kernel_buf) return -ENOMEM;
    
    int ret = copy_from_user(kernel_buf, buf, count);
    if (IS_ERROR(ret)) {
        if (count <= 4096 && pipe_temp_cache) slub_free(pipe_temp_cache, kernel_buf);
        else kfree(kernel_buf);
        return ret;
    }
    
    ssize_t bytes_written = pipe_write_buffer(pipe, kernel_buf, count);
    
    if (count <= 4096 && pipe_temp_cache) slub_free(pipe_temp_cache, kernel_buf);
    else kfree(kernel_buf);
    return bytes_written;
}

int pipe_close_by_id(int pipe_id, int is_write_end) {
    pipe_buffer_t *pipe = hash_table_lookup(&pipe_registry.pipe_table, pipe_id);
    if (!pipe) return -EBADF;
    
    if (is_write_end) {
        pipe->writers--;
    } else {
        pipe->readers--;
    }
    
    if (pipe->readers <= 0 && pipe->writers <= 0) {
        hash_table_remove(&pipe_registry.pipe_table, pipe_id);
        pipe_registry.active_pipes--;
        pipe_destroy_buffer(pipe);
    }
    
    return 0;
}

void pipe_subsystem_init(void) {
    pipe_registry_init();
}
