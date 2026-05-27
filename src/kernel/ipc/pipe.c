/*
 * pipe.c — POSIX pipe(2) IPC: registry of pipe_buffer_t (ring + 2 wait-queues +
 * reader/writer counts), sys_pipe and the *_by_id helpers routed by sys_fs.c on
 * fds >= PIPE_FD_BASE.
 *
 * Invariants:
 *  - fd encoding: proc->fd_table[fd].of_idx == PIPE_FD_BASE + pipe_id, with
 * O_RDONLY/O_WRONLY in .flags distinguishing the two ends of the same pair.
 *  - pipe->lock protects data, read_pos, write_pos, count, readers, writers,
 * flags; pipe_reg_lock protects only the registry table and is released before
 * pipe->lock is taken.
 *  - Sleep protocol: prepare on rd_wq/wr_wq, drop pipe->lock, schedule(),
 * reacquire pipe->lock, re-check predicate; wake_all is issued only AFTER
 * releasing pipe->lock.
 *  - EOF for readers = (writers == 0 && count == 0) returns 0; EPIPE for
 * writers = (readers == 0) returns -EPIPE and delivers SIGPIPE while still
 * holding pipe->lock for atomic observe+signal.
 *  - pipe_buffer_t lifetime is governed by readers+writers acting as refcount:
 * destroy only when the last close observes readers == 0 && writers == 0.
 *
 * Not allowed:
 *  - Calling schedule() outside the prepare/drop/schedule/reacquire/finish
 * protocol, or holding pipe->lock across schedule() or wake_*.
 *  - Exposing pipe_buffer_t (or its fields) outside this translation unit; the
 * only public handle is the integer pipe_id.
 *  - Dereferencing user pointers (pipefd, buf) without
 * copy_to_user/copy_from_user.
 */

#include <kernel/console.h>
#include <kernel/errno.h>
#include <kernel/hashtable.h>
#include <kernel/kernel.h>
#include <kernel/kstring.h>
#include <kernel/memory.h>
#include <kernel/pipe.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>
#include <kernel/signal.h>
#include <kernel/uaccess.h>
#include <uapi/syscalls.h>

#define PIPE_BUF 4096
#define PIPE_MAX_SIZE 65536
#include <kernel/constants.h>
#include <kernel/spinlock.h>
#include <kernel/types.h>
#include <kernel/waitq.h>

typedef struct pipe_buffer {
  char *data;
  size_t size;
  size_t read_pos;
  size_t write_pos;
  size_t count;
  uint32_t readers;
  uint32_t writers;
  uint32_t flags;
  spinlock_t lock;
  wait_queue_t rd_wq;
  wait_queue_t wr_wq;
  hash_entry_t ht_node;
} pipe_buffer_t;

typedef struct pipe_registry {
  hash_table_t pipe_table;
  uint32_t next_pipe_id;
  uint32_t active_pipes;
} pipe_registry_t;

static pipe_registry_t pipe_registry;

static spinlock_t pipe_reg_lock = SPINLOCK_INIT("pipe_reg");

static pipe_buffer_t *pipe_create_buffer(size_t size) {
  if (size > PIPE_MAX_SIZE)
    size = PIPE_MAX_SIZE;
  if (size < PIPE_BUF)
    size = PIPE_BUF;

  pipe_buffer_t *pipe = kmalloc(sizeof(pipe_buffer_t));
  if (!pipe)
    return NULL;

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
  wait_queue_init(&pipe->rd_wq, "pipe_rd");
  wait_queue_init(&pipe->wr_wq, "pipe_wr");

  return pipe;
}

static void pipe_destroy_buffer(pipe_buffer_t *pipe) {
  if (!pipe)
    return;
  if (pipe->data)
    kfree(pipe->data);
  kfree(pipe);
}

static ssize_t pipe_read_buffer(pipe_buffer_t *pipe, char *buf, size_t count) {
  spin_lock(&pipe->lock);

  if (pipe->writers == 0 && pipe->count == 0) {
    spin_unlock(&pipe->lock);
    return 0;
  }

  while (pipe->count == 0) {
    if (pipe->writers == 0) {
      spin_unlock(&pipe->lock);
      return 0; /* EOF — no more writers. */
    }
    if (pipe->flags & O_NONBLOCK) {
      spin_unlock(&pipe->lock);
      return -EAGAIN;
    }
    if (signal_pending_check()) {
      spin_unlock(&pipe->lock);
      return -EINTR;
    }
    wait_event_locked(&pipe->rd_wq, &pipe->lock,
                      pipe->count > 0 || pipe->writers == 0);
  }

  size_t to_read = (count < pipe->count) ? count : pipe->count;
  size_t first = pipe->size - pipe->read_pos;
  if (first > to_read)
    first = to_read;
  kmemcpy(buf, pipe->data + pipe->read_pos, first);
  if (to_read > first) {
    kmemcpy(buf + first, pipe->data, to_read - first);
  }
  pipe->read_pos = (pipe->read_pos + to_read) % pipe->size;
  pipe->count -= to_read;

  spin_unlock(&pipe->lock);
  if (to_read > 0)
    wake_all(&pipe->wr_wq);
  return (ssize_t)to_read;
}

static ssize_t pipe_write_buffer(pipe_buffer_t *pipe, const char *buf,
                                 size_t count) {
  spin_lock(&pipe->lock);

  if (pipe->readers == 0) {
    process_t *cur = get_current_process();
    if (cur)
      process_send_signal(cur->pid, SIGPIPE);
    spin_unlock(&pipe->lock);
    return -EPIPE;
  }

  while (pipe->count == pipe->size) {
    if (pipe->readers == 0) {
      process_t *cur = get_current_process();
      if (cur)
        process_send_signal(cur->pid, SIGPIPE);
      spin_unlock(&pipe->lock);
      return -EPIPE;
    }
    if (pipe->flags & O_NONBLOCK) {
      spin_unlock(&pipe->lock);
      return -EAGAIN;
    }
    if (signal_pending_check()) {
      spin_unlock(&pipe->lock);
      return -EINTR;
    }
    wait_event_locked(&pipe->wr_wq, &pipe->lock,
                      pipe->count < pipe->size || pipe->readers == 0);
  }

  size_t available = pipe->size - pipe->count;
  size_t to_write = (count < available) ? count : available;
  size_t first = pipe->size - pipe->write_pos;
  if (first > to_write)
    first = to_write;
  kmemcpy(pipe->data + pipe->write_pos, buf, first);
  if (to_write > first) {
    kmemcpy(pipe->data, buf + first, to_write - first);
  }
  pipe->write_pos = (pipe->write_pos + to_write) % pipe->size;
  pipe->count += to_write;

  spin_unlock(&pipe->lock);
  if (to_write > 0)
    wake_all(&pipe->rd_wq);
  return (ssize_t)to_write;
}

static void pipe_registry_init(void) {
  hash_table_init(&pipe_registry.pipe_table, "pipes");
  pipe_registry.next_pipe_id = 1;
  pipe_registry.active_pipes = 0;
}

static int pipe_allocate_id(pipe_buffer_t *pipe) {
  spin_lock(&pipe_reg_lock);
  uint32_t pipe_id = pipe_registry.next_pipe_id++;
  int ret = hash_table_insert(&pipe_registry.pipe_table, pipe_id, pipe,
                              &pipe->ht_node);
  if (ret == 0) {
    pipe_registry.active_pipes++;
    spin_unlock(&pipe_reg_lock);
    return pipe_id;
  }
  spin_unlock(&pipe_reg_lock);
  return -1;
}

static void pipe_registry_remove(int pipe_id) {
  spin_lock(&pipe_reg_lock);
  hash_table_remove(&pipe_registry.pipe_table, pipe_id, NULL);
  pipe_registry.active_pipes--;
  spin_unlock(&pipe_reg_lock);
}

static pipe_buffer_t *pipe_registry_lookup(uint32_t pipe_id) {
  spin_lock(&pipe_reg_lock);
  pipe_buffer_t *p = hash_table_lookup(&pipe_registry.pipe_table, pipe_id);
  spin_unlock(&pipe_reg_lock);
  return p;
}

int sys_pipe(int pipefd[2]) {
  if (!pipefd)
    return -EINVAL;

  int kernel_fds[2] = {-1, -1};

  pipe_buffer_t *pipe = pipe_create_buffer(PIPE_BUF);
  if (!pipe)
    return -ENOMEM;

  int pipe_id = pipe_allocate_id(pipe);
  if (pipe_id < 0) {
    pipe_destroy_buffer(pipe);
    return -EMFILE;
  }

  process_t *proc = get_current_process();
  if (!proc) {
    pipe_registry_remove(pipe_id);
    pipe_destroy_buffer(pipe);
    return -ESRCH;
  }

  int found = 0;
  for (int i = 3; i < MAX_OPEN_FILES_CONST && found < 2; i++) {
    if (proc->fd_table[i].of_idx == 0 && proc->fd_table[i].flags == 0) {
      kernel_fds[found] = i;
      proc->fd_table[i].of_idx = PIPE_FD_BASE + pipe_id;
      proc->fd_table[i].flags = (found == 0) ? O_RDONLY : O_WRONLY;
      proc->fd_table[i].offset = 0;
      proc->fd_table[i].refcount = 1;
      found++;
    }
  }

  if (found < 2) {
    for (int i = 0; i < found; i++) {
      proc->fd_table[kernel_fds[i]].of_idx = 0;
      proc->fd_table[kernel_fds[i]].flags = 0;
    }
    pipe_registry_remove(pipe_id);
    pipe_destroy_buffer(pipe);
    return -EMFILE;
  }

  pipe->readers = 1;
  pipe->writers = 1;

  int ret = copy_to_user(pipefd, kernel_fds, sizeof(kernel_fds));
  if (IS_ERROR(ret)) {
    proc->fd_table[kernel_fds[0]].of_idx = 0;
    proc->fd_table[kernel_fds[0]].flags = 0;
    proc->fd_table[kernel_fds[1]].of_idx = 0;
    proc->fd_table[kernel_fds[1]].flags = 0;
    pipe_registry_remove(pipe_id);
    pipe_destroy_buffer(pipe);
    return ret;
  }

  return 0;
}

int is_pipe_fd(uint32_t fd, int *pipe_id) {
  if (fd >= MAX_OPEN_FILES_CONST)
    return 0;
  uint32_t node_idx = get_current_process()->fd_table[fd].of_idx;
  if (node_idx >= PIPE_FD_BASE && node_idx != CONSOLE_INODE_MAGIC) {
    if (pipe_id)
      *pipe_id = node_idx - PIPE_FD_BASE;
    return 1;
  }
  return 0;
}

ssize_t pipe_read_by_id(int pipe_id, void *buf, size_t count) {
  pipe_buffer_t *pipe = pipe_registry_lookup(pipe_id);
  if (!pipe)
    return -EBADF;

  char *kernel_buf = kmalloc(count);
  if (!kernel_buf)
    return -ENOMEM;

  ssize_t bytes_read = pipe_read_buffer(pipe, kernel_buf, count);

  if (bytes_read > 0) {
    int ret = copy_to_user(buf, kernel_buf, bytes_read);
    if (IS_ERROR(ret)) {
      kfree(kernel_buf);
      return ret;
    }
  }

  kfree(kernel_buf);
  return bytes_read;
}

ssize_t pipe_write_by_id(int pipe_id, const void *buf, size_t count) {
  pipe_buffer_t *pipe = pipe_registry_lookup(pipe_id);
  if (!pipe)
    return -EBADF;

  char *kernel_buf = kmalloc(count);
  if (!kernel_buf)
    return -ENOMEM;

  int ret = copy_from_user(kernel_buf, buf, count);
  if (IS_ERROR(ret)) {
    kfree(kernel_buf);
    return ret;
  }

  ssize_t bytes_written = pipe_write_buffer(pipe, kernel_buf, count);

  kfree(kernel_buf);
  return bytes_written;
}

int pipe_close_by_id(int pipe_id, int is_write_end) {
  pipe_buffer_t *pipe = pipe_registry_lookup(pipe_id);
  if (!pipe)
    return -EBADF;

  spin_lock(&pipe->lock);
  if (is_write_end)
    pipe->writers--;
  else
    pipe->readers--;
  int readers_now = pipe->readers;
  int writers_now = pipe->writers;
  spin_unlock(&pipe->lock);

  wake_all(&pipe->rd_wq);
  wake_all(&pipe->wr_wq);

  if (readers_now == 0 && writers_now == 0) {
    pipe_registry_remove(pipe_id);
    pipe_destroy_buffer(pipe);
  }

  return 0;
}

void pipe_subsystem_init(void) { pipe_registry_init(); }
