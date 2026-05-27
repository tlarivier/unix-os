/*
 * percpu.c — define cpus[MAX_CPUS] storage and out-of-line preempt bodies.
 *
 * Invariants:
 *  - cpus[i].self == &cpus[i] for every i < MAX_CPUS once percpu_init_bsp
 *    has run, including slots whose AP never boots (GDT GS descriptors
 *    point at every slot before bringup; %gs:0 must load a valid self).
 *  - cpu_count starts at 1 (BSP-only sentinel) and is later set by smp_init
 *    once ACPI topology is known.
 *  - preempt_disable/enable bracket every region that uses this_cpu(); the
 *    paired enable restores the exact prior preempt_count.
 *  - percpu_init_bsp runs once on the BSP before gdt_init loads %gs.
 *
 * Not allowed:
 *  - Call any function that itself uses this_cpu() before percpu_init_bsp
 *    has returned and gdt_init has loaded the per-CPU GS selector.
 *  - Take spinlocks or allocate memory inside preempt_disable/enable
 *    (they are intentionally minimal; only touch cpus[].preempt_count).
 *  - Expose cpus[]/cpu_count writers other than percpu_init_bsp + smp_init.
 */

#include <kernel/gdt.h>
#include <kernel/percpu.h>
#include <kernel/spinlock.h>
#include <stddef.h>

cpu_t cpus[MAX_CPUS];
uint32_t cpu_count = 1;

void preempt_disable(void) {
  this_cpu()->preempt_count++;
  __asm__ volatile("" ::: "memory");
}

void preempt_enable(void) {
  __asm__ volatile("" ::: "memory");
  this_cpu()->preempt_count--;
}

int preempt_count(void) { return this_cpu()->preempt_count; }

void percpu_init_bsp(void) {
  for (uint32_t i = 0; i < MAX_CPUS; i++) {
    cpus[i].self = &cpus[i];
    cpus[i].id = i;
    cpus[i].lapic_id = 0;
    cpus[i].current_proc = NULL;
    cpus[i].preempt_count = 0;
    cpus[i].intr_depth = 0;
    cpus[i].kernel_stack = NULL;
    spinlock_init(&cpus[i].rq_lock, "rq_lock");
  }
}

void percpu_init_ap(uint32_t cpu_id) {
  gdt_load_bsp_pointer();
  gs_load_for_cpu(cpu_id);
  tr_load_for_cpu(cpu_id);
}
