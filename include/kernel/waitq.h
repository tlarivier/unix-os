#ifndef KERNEL_WAITQ_H
#define KERNEL_WAITQ_H

#include <kernel/spinlock.h>
#include <stdint.h>

struct process;

typedef struct wait_queue_entry {
  struct process *proc;
  struct wait_queue_entry *next;
} wait_queue_entry_t;

typedef struct wait_queue {
  spinlock_t lock;
  wait_queue_entry_t *head;
} wait_queue_t;

#define WAIT_QUEUE_INIT(name_str)                                              \
  {.lock = SPINLOCK_INIT(name_str), .head = NULL}

void wait_queue_init(wait_queue_t *wq, const char *name);
void wait_queue_prepare(wait_queue_t *wq, wait_queue_entry_t *entry);
void wait_queue_finish(wait_queue_t *wq, wait_queue_entry_t *entry);
void wake_all(wait_queue_t *wq);

#define wait_event(wq_ptr, cond)                                               \
  do {                                                                         \
    wait_queue_entry_t __wqe = {.proc = NULL, .next = NULL};                   \
    for (;;) {                                                                 \
      if (cond)                                                                \
        break;                                                                 \
      wait_queue_prepare((wq_ptr), &__wqe);                                    \
      if (cond) {                                                              \
        wait_queue_finish((wq_ptr), &__wqe);                                   \
        break;                                                                 \
      }                                                                        \
      extern void schedule(void);                                              \
      schedule();                                                              \
      wait_queue_finish((wq_ptr), &__wqe);                                     \
    }                                                                          \
  } while (0)

#define wait_event_locked(wq_ptr, lock_ptr, cond)                              \
  do {                                                                         \
    wait_queue_entry_t __wqe = {.proc = NULL, .next = NULL};                   \
    for (;;) {                                                                 \
      if (cond)                                                                \
        break;                                                                 \
      wait_queue_prepare((wq_ptr), &__wqe);                                    \
      spin_unlock((lock_ptr));                                                 \
      extern void schedule(void);                                              \
      schedule();                                                              \
      wait_queue_finish((wq_ptr), &__wqe);                                     \
      spin_lock((lock_ptr));                                                   \
    }                                                                          \
  } while (0)

#endif
