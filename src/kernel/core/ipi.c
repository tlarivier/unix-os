/*
 * ipi.c — hot-path SMP signalling: smp_ipi_send, ipi_dispatch and the
 *         broadcast TLB shootdown (smp_tlb_flush_all + ack counter).
 *
 * Invariants:
 *  - ipi_dispatch acks the LAPIC (lapic_eoi) BEFORE handing off to
 *    schedule() on IPI_VEC_RESCHED, because schedule() may never return
 *    into this frame and leaving ISR set would mask further IPIs.
 *  - g_tlb_flush_ack is touched only on the IPI path; initiator zeroes
 *    it (RELAXED — ICR write serialises), targets increment with RELEASE,
 *    initiator waits with ACQUIRE so the CR3 reload is observed.
 *  - smp_ipi_send is a thin wrapper that resolves cpu id -> APIC id
 *    via cpus[]; caller owns any synchronisation beyond delivery.
 *  - On TLB shootdown each target reloads CR3 (full non-global flush);
 *    no per-page or per-mm narrowing is performed today.
 *
 * Not allowed:
 *  - Allocating, touching VFS or taking sleeping locks from any handler.
 *  - Reordering EOI vs RESCHED in ipi_dispatch — see audit/02-init.md F1.
 *  - Sending IPIs to cpu ids >= cpu_count (the send wrapper drops them).
 */

#include <kernel/lapic.h>
#include <kernel/percpu.h>
#include <kernel/scheduler.h>
#include <kernel/smp.h>
#include <stdint.h>

static volatile uint32_t g_tlb_flush_ack;

void smp_ipi_send(uint32_t target_cpu, uint8_t vector) {
  if (target_cpu >= cpu_count)
    return;
  lapic_send_ipi(cpus[target_cpu].lapic_id, vector);
}

void smp_tlb_flush_all(void) {
  if (cpu_count <= 1)
    return;

  __atomic_store_n(&g_tlb_flush_ack, 0, __ATOMIC_RELAXED);

  uint32_t me = this_cpu()->id;
  uint32_t expected = 0;
  for (uint32_t i = 0; i < cpu_count; i++) {
    if (i == me)
      continue;
    smp_ipi_send(i, IPI_VEC_TLB_FLUSH);
    expected++;
  }

  while (__atomic_load_n(&g_tlb_flush_ack, __ATOMIC_ACQUIRE) < expected) {
    __asm__ volatile("pause" ::: "memory");
  }
}

void ipi_dispatch(uint32_t vector) {
  if (vector == IPI_VEC_TLB_FLUSH) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" ::"r"(cr3) : "memory");
    __atomic_add_fetch(&g_tlb_flush_ack, 1, __ATOMIC_RELEASE);
  }

  lapic_eoi();

  if (vector == IPI_VEC_RESCHED) {
    schedule();
  }
}
