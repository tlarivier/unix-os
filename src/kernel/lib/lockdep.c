/*
 * lockdep.c — lock-ordering validator: BFS over a global class/edge
 *             graph at each acquire, panic on cycle closure.
 *
 * Invariants:
 *  - graph_lock is a raw __atomic_exchange_n (never a spinlock_t) so
 *    spin_lock -> lockdep_acquire -> spin_lock cannot recurse.
 *  - Held-stack lives in cpu_t.lockdep_stack[32] (per-CPU); class table
 *    and edge graph are global behind graph_lock.
 *  - IRQs are off (cli) for the full body of lockdep_acquire/release
 *    via lockdep_irq_save/restore.
 *  - On cycle detection, panic immediately via __debug_trap — never a
 *    soft warning.
 *
 * Not allowed:
 *  - Using spinlock_t internally (inclusion cycle).
 *  - kmalloc / slub_alloc — pools are fixed (LOCKDEP_MAX_CLASSES=256,
 *    LOCKDEP_MAX_EDGES=1024).
 *  - Calling kprintf with IRQs off (use kprintf_panic instead).
 */

#include <kernel/kernel.h>
#include <kernel/kprintf.h>
#include <kernel/kstring.h>
#include <kernel/lockdep.h>
#include <kernel/percpu.h>
#include <stddef.h>
#include <stdint.h>

#define __debug_trap(msg) kernel_panic((msg), __FILE__, __LINE__)

#ifdef CONFIG_LOCKDEP

#define LOCKDEP_MAX_CLASSES 256
#define LOCKDEP_MAX_EDGES 1024
#define LOCKDEP_MAX_HELD 32

struct lockdep_class {
  const char *name;
};

struct lockdep_edge {
  uint8_t from;
  uint8_t to;
};

static struct lockdep_class g_classes[LOCKDEP_MAX_CLASSES];
static int g_num_classes;

static struct lockdep_edge g_edges[LOCKDEP_MAX_EDGES];
static int g_num_edges;

static volatile uint32_t g_graph_lock = 0;

static volatile uint32_t g_overflow_warned;
static volatile uint32_t g_initialized = 1;

static inline void graph_lock(void) {
  while (__atomic_exchange_n(&g_graph_lock, 1u, __ATOMIC_ACQUIRE)) {
    __asm__ volatile("pause" ::: "memory");
  }
}
static inline void graph_unlock(void) {
  __atomic_store_n(&g_graph_lock, 0u, __ATOMIC_RELEASE);
}

static inline int try_set_overflow_warned(void) {
  uint32_t expected = 0;
  return __atomic_compare_exchange_n(&g_overflow_warned, &expected, 1u, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

static inline uint32_t lockdep_irq_save(void) {
  uint32_t f;
  __asm__ volatile("pushf; pop %0; cli" : "=r"(f)::"memory");
  return f;
}

static inline void lockdep_irq_restore(uint32_t f) {
  __asm__ volatile("push %0; popf" ::"r"(f) : "memory", "cc");
}

static int find_or_register_class_locked(const char *name) {
  if (!name)
    name = "<unnamed>";
  for (int i = 0; i < g_num_classes; i++) {
    if (kstrcmp(g_classes[i].name, name) == 0)
      return i;
  }
  if (g_num_classes >= LOCKDEP_MAX_CLASSES) {
    if (try_set_overflow_warned()) {
      kprintf_panic("lockdep: class table full, new locks untracked\n");
    }
    return -1;
  }
  g_classes[g_num_classes].name = name;
  return g_num_classes++;
}

static int find_class_locked(const char *name) {
  if (!name)
    name = "<unnamed>";
  for (int i = 0; i < g_num_classes; i++) {
    if (kstrcmp(g_classes[i].name, name) == 0)
      return i;
  }
  return -1;
}

static int reachable(int src, int dst) {
  if (src == dst)
    return 1;
  uint8_t visited[LOCKDEP_MAX_CLASSES] = {0};
  int queue[LOCKDEP_MAX_CLASSES];
  int q_head = 0, q_tail = 0;

  visited[src] = 1;
  queue[q_tail++] = src;

  while (q_head < q_tail) {
    int curr = queue[q_head++];
    for (int e = 0; e < g_num_edges; e++) {
      if (g_edges[e].from != curr)
        continue;
      int next = g_edges[e].to;
      if (next == dst)
        return 1;
      if (!visited[next]) {
        visited[next] = 1;
        queue[q_tail++] = next;
      }
    }
  }
  return 0;
}

static int add_edge_locked(int from, int to) {
  for (int e = 0; e < g_num_edges; e++) {
    if (g_edges[e].from == from && g_edges[e].to == to)
      return 0;
  }
  if (g_num_edges >= LOCKDEP_MAX_EDGES) {
    if (try_set_overflow_warned()) {
      kprintf_panic("lockdep: edge table full, ordering untracked\n");
    }
    return 0;
  }
  g_edges[g_num_edges].from = (uint8_t)from;
  g_edges[g_num_edges].to = (uint8_t)to;
  g_num_edges++;
  return 1;
}

void lockdep_acquire(const char *name) {
  if (!__atomic_load_n(&g_initialized, __ATOMIC_ACQUIRE))
    return;
  uint32_t f = lockdep_irq_save();
  cpu_t *me = this_cpu();

  graph_lock();
  int new_id = find_or_register_class_locked(name);
  if (new_id < 0) {
    graph_unlock();
    lockdep_irq_restore(f);
    return;
  }

  for (int i = 0; i < me->lockdep_depth; i++) {
    int prior = me->lockdep_stack[i];
    if (prior == new_id)
      continue; /* same-class re-entry: benign */
    if (reachable(new_id, prior)) {
      kprintf_panic("\n*** LOCKDEP CYCLE DETECTED ***\n");
      kprintf_panic("  acquiring '");
      kprintf_panic(g_classes[new_id].name);
      kprintf_panic("' would close a cycle with prior '");
      kprintf_panic(g_classes[prior].name);
      kprintf_panic("'\n");
      graph_unlock();
      lockdep_irq_restore(f);
      __debug_trap("lockdep cycle");
    }
    add_edge_locked(prior, new_id);
  }
  graph_unlock();

  if (me->lockdep_depth < LOCKDEP_MAX_HELD) {
    me->lockdep_stack[me->lockdep_depth++] = new_id;
  } else {
    if (try_set_overflow_warned()) {
      kprintf_panic("lockdep: held-stack overflow\n");
    }
  }

  lockdep_irq_restore(f);
}

void lockdep_release(const char *name) {
  if (!__atomic_load_n(&g_initialized, __ATOMIC_ACQUIRE))
    return;
  uint32_t f = lockdep_irq_save();
  cpu_t *me = this_cpu();

  graph_lock();
  int id = find_class_locked(name);
  graph_unlock();
  if (id < 0) {
    lockdep_irq_restore(f);
    return;
  }

  if (me->lockdep_depth == 0) {
    kprintf_panic("lockdep: release on empty held-stack\n");
    lockdep_irq_restore(f);
    return;
  }
  int top = me->lockdep_stack[me->lockdep_depth - 1];
  if (top == id) {
    me->lockdep_depth--;
  } else {
    int found = -1;
    for (int i = me->lockdep_depth - 1; i >= 0; i--) {
      if (me->lockdep_stack[i] == id) {
        found = i;
        break;
      }
    }
    if (found < 0) {
      kprintf_panic("lockdep: release of lock not in held-stack\n");
    } else {
      for (int i = found; i < me->lockdep_depth - 1; i++) {
        me->lockdep_stack[i] = me->lockdep_stack[i + 1];
      }
      me->lockdep_depth--;
    }
  }

  lockdep_irq_restore(f);
}

#endif /* CONFIG_LOCKDEP */
