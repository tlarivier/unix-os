/*
 * gdt.c — build the shared GDT (standard segments + per-CPU TSS and
 * GS-base descriptors) and expose gdt_init/tss_init_bsp/percpu_init_ap/
 * tss_set_kernel_stack to bring up each CPU's ring-0 return and this_cpu().
 *
 * Invariants:
 *  - Entries 1..4 are fixed: kernel CS 0x08, kernel DS 0x10, user CS 0x1B,
 *    user DS 0x23; no other entry reuses these selectors.
 *  - tss[id].esp0 is written only by CPU id itself via tss_set_kernel_stack;
 *    its TSS is loaded once with ltr by tss_init_bsp (cpu 0) or
 *    percpu_init_ap (APs).
 *  - After percpu_init_ap(i), `mov %gs:0, %reg` returns &cpus[i] (per-CPU
 *    GS descriptor base = &cpus[i], limit = sizeof(cpu_t)).
 *  - GDT_FIRST_GS_SEL is the single source of truth for the per-CPU GS
 *    selector base shared with isr.asm / syscall_entry.S (compile-time
 *    asserted against the runtime layout).
 *
 * Not allowed:
 *  - Expose struct gdt_entry / struct tss_entry outside arch/ (live in
 *    gdt_internal.h, included only by this TU).
 *  - Call scheduler, VFS, mm, or timer subsystems from here.
 *  - Hold a spinlock or take an IPI/IRQ-masked section in this file.
 */

#include "kernel/gdt.h"
#include "gdt_internal.h"
#include "kernel/constants.h"
#include "kernel/percpu.h"

#define STANDARD_ENTRIES 5
#define GDT_ENTRIES (STANDARD_ENTRIES + 2 * MAX_CPUS)

#define TSS_ENTRY(cpu_id) (STANDARD_ENTRIES + (cpu_id))
#define GS_ENTRY(cpu_id) (STANDARD_ENTRIES + MAX_CPUS + (cpu_id))

#define TSS_SEL_FOR(cpu_id) (uint16_t)((TSS_ENTRY(cpu_id) << 3) | 3)
#define GS_SEL_FOR(cpu_id) (uint16_t)((GS_ENTRY(cpu_id) << 3))

_Static_assert(GDT_FIRST_GS_SEL == (STANDARD_ENTRIES + MAX_CPUS) * 8,
               "GDT_FIRST_GS_SEL out of sync with GDT layout — update "
               "isr.asm / syscall_entry.S to match");

static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t gdt_ptr;
static tss_entry_t tss[MAX_CPUS] __attribute__((aligned(16)));

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access,
                         uint8_t gran) {
  gdt[num].base_low = (base & 0xFFFF);
  gdt[num].base_middle = (base >> 16) & 0xFF;
  gdt[num].base_high = (base >> 24) & 0xFF;

  gdt[num].limit_low = (limit & 0xFFFF);
  gdt[num].granularity = (limit >> 16) & 0x0F;
  gdt[num].granularity |= gran & 0xF0;
  gdt[num].access = access;
}

static inline void load_gs(uint16_t sel) {
  __asm__ volatile("movw %0, %%gs" ::"r"(sel) : "memory");
}

static inline void load_tr(uint16_t sel) {
  __asm__ volatile("ltr %0" ::"r"(sel));
}

void gdt_init(void) {
  gdt_ptr.limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) - 1;
  gdt_ptr.base = (uint32_t)&gdt;

  gdt_set_gate(0, 0, 0, 0, 0);
  gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); /* kernel code */
  gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); /* kernel data */
  gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); /* user code   */
  gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); /* user data   */

  for (uint32_t i = 0; i < MAX_CPUS; i++) {
    uint32_t base = (uint32_t)&tss[i];
    uint32_t limit = sizeof(tss_entry_t) - 1;
    gdt_set_gate(TSS_ENTRY(i), base, limit, 0x89, 0x00);

    for (size_t b = 0; b < sizeof(tss_entry_t); b++)
      ((uint8_t *)&tss[i])[b] = 0;
    tss[i].ss0 = KERNEL_DATA_SEL;
    tss[i].esp0 = 0;
    tss[i].cs = KERNEL_CODE_SEL | 3;
    tss[i].ss = tss[i].ds = tss[i].es = tss[i].fs = tss[i].gs =
        KERNEL_DATA_SEL | 3;
    tss[i].iomap_base = sizeof(tss_entry_t);
  }

  for (uint32_t i = 0; i < MAX_CPUS; i++) {
    uint32_t base = (uint32_t)&cpus[i];
    uint32_t limit = sizeof(cpu_t) - 1;
    gdt_set_gate(GS_ENTRY(i), base, limit, 0x92, 0x40);
  }

  gdt_flush((uint32_t)&gdt_ptr);

  load_gs(GS_SEL_FOR(0));
}

void tss_init_bsp(void) { load_tr(TSS_SEL_FOR(0)); }

void tss_set_kernel_stack(uint32_t esp0) { tss[this_cpu()->id].esp0 = esp0; }

void gdt_load_bsp_pointer(void) {
  __asm__ volatile("lgdt (%0)" ::"r"(&gdt_ptr) : "memory");
}

void gs_load_for_cpu(uint32_t cpu_id) { load_gs(GS_SEL_FOR(cpu_id)); }

void tr_load_for_cpu(uint32_t cpu_id) { load_tr(TSS_SEL_FOR(cpu_id)); }
