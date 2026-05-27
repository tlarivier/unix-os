/*
 * smp_bringup.c — bring up APs via INIT-SIPI-SIPI and run ap_main().
 *
 * Invariants:
 *  - smp_bring_up_aps runs once on the BSP, after LAPIC and scheduler init.
 *  - ap_main never returns; it ends by context-switching into its idle proc.
 *  - ap_ready_count is monotonic: only ap_main increments it (RELEASE),
 *    only the BSP reads it (ACQUIRE) to wait for readiness.
 *  - cpus[i].self is non-NULL for every i < MAX_CPUS before any AP loads %gs.
 *  - The rendezvous slot at low physical memory (0x9000..) is the only
 *    pre-paging channel between BSP and the AP trampoline.
 *
 * Not allowed:
 *  - Call from outside init (single-shot bringup, not a runtime API).
 *  - Mutate cpus[] topology fields (lapic_id, cpu_count) — that is smp.c.
 *  - Send IPIs or calibrate the LAPIC timer here (core/ipi.c, arch/x86).
 */
#include <kernel/idt.h>
#include <kernel/kprintf.h>
#include <kernel/kstring.h>
#include <kernel/lapic.h>
#include <kernel/percpu.h>
#include <kernel/process.h>
#include <kernel/sched_arch.h>
#include <kernel/scheduler.h>
#include <kernel/smp.h>
#include <kernel/timer.h>
#include <stddef.h>
#include <stdint.h>

static const uint8_t ap_tramp_bin_data[] = {
    0xfa, 0xfc, 0x66, 0x0f, 0x01, 0x16, 0x70, 0x80, 0x0f, 0x20, 0xc0, 0x66,
    0x83, 0xc8, 0x01, 0x0f, 0x22, 0xc0, 0x66, 0xea, 0x1a, 0x80, 0x00, 0x00,
    0x08, 0x00, 0x66, 0xb8, 0x10, 0x00, 0x8e, 0xd8, 0x8e, 0xc0, 0x8e, 0xd0,
    0x8e, 0xe0, 0x8e, 0xe8, 0xa1, 0x00, 0x90, 0x00, 0x00, 0x0f, 0x22, 0xd8,
    0x0f, 0x20, 0xc0, 0x0d, 0x00, 0x00, 0x00, 0x80, 0x0f, 0x22, 0xc0, 0x8b,
    0x25, 0x04, 0x90, 0x00, 0x00, 0xa1, 0x0c, 0x90, 0x00, 0x00, 0x50, 0xa1,
    0x08, 0x90, 0x00, 0x00, 0xff, 0xd0, 0xfa, 0xf4, 0xeb, 0xfc, 0x8d, 0xb6,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x9a, 0xcf, 0x00, 0xff, 0xff, 0x00, 0x00,
    0x00, 0x92, 0xcf, 0x00, 0x17, 0x00, 0x58, 0x80, 0x00, 0x00};

#define TRAMP_PHYS 0x8000u
#define RENDEZ_PHYS 0x9000u

#define AP_STACK_BYTES 16384
static uint8_t ap_stacks[MAX_CPUS][AP_STACK_BYTES] __attribute__((aligned(16)));

static volatile uint32_t ap_ready_count;

static inline void mmio_write32(uint32_t phys_addr, uint32_t value) {
  *(volatile uint32_t *)phys_addr = value;
}

void ap_idle_loop(void) {
  for (;;) {
    __asm__ volatile("hlt" ::: "memory");
  }
}

void ap_main(uint32_t logical_cpu_id) {
  percpu_init_ap(logical_cpu_id);
  idt_init_ap();
  lapic_init();
  lapic_timer_start(g_lapic_timer_initial ? g_lapic_timer_initial : 10000000u);

  char idle_name[16];
  kstrncpy(idle_name, "idle-cpu0", sizeof(idle_name));
  idle_name[8] = '0' + (char)logical_cpu_id;
  process_t *idle = process_create(idle_name, (void *)ap_idle_loop);
  if (!idle) {
    kprintf("AP: process_create idle FAILED on cpu%u\n", logical_cpu_id);
    for (;;)
      __asm__ volatile("cli; hlt");
  }
  __atomic_store_n(&idle->state, PROCESS_RUNNING, __ATOMIC_RELEASE);
  this_cpu()->current_proc = idle;
  scheduler_register_idle(logical_cpu_id, idle);

  __atomic_add_fetch(&ap_ready_count, 1, __ATOMIC_RELEASE);

  asm_context_switch(NULL, &idle->context);

  for (;;)
    __asm__ volatile("cli; hlt");
}

int smp_bring_up_aps(void) {
  if (cpu_count <= 1) {
    kprintf("SMP: only 1 CPU, nothing to bring up\n");
    return 1;
  }

  kmemcpy((void *)TRAMP_PHYS, ap_tramp_bin_data, sizeof(ap_tramp_bin_data));

  mmio_write32(RENDEZ_PHYS + 0x00, (uint32_t)kernel_page_directory);
  mmio_write32(RENDEZ_PHYS + 0x08, (uint32_t)&ap_main);

  for (uint32_t i = 1; i < cpu_count; i++) {
    uint32_t apic_id = cpus[i].lapic_id;

    uint32_t stack_top = (uint32_t)&ap_stacks[i][AP_STACK_BYTES];
    mmio_write32(RENDEZ_PHYS + 0x04, stack_top);
    mmio_write32(RENDEZ_PHYS + 0x0C, i);

    cpus[i].kernel_stack = (void *)stack_top;

    /* Publish rendezvous bytes before the SIPI: x86 TSO makes the volatile
     * stores already ordered, but the explicit release fence documents the
     * "AP must see these writes" contract. */
    __atomic_thread_fence(__ATOMIC_RELEASE);

    lapic_send_init(apic_id);
    sleep_ms(10);
    lapic_send_startup(apic_id, (uint8_t)(TRAMP_PHYS >> 12));
    sleep_ms(1);
    lapic_send_startup(apic_id, (uint8_t)(TRAMP_PHYS >> 12));

    uint32_t expected = i;
    int waited_ms = 0;
    while (__atomic_load_n(&ap_ready_count, __ATOMIC_ACQUIRE) < expected &&
           waited_ms < 200) {
      sleep_ms(10);
      waited_ms += 10;
    }
    if (__atomic_load_n(&ap_ready_count, __ATOMIC_ACQUIRE) < expected) {
      kprintf("SMP: cpu%u did not ack within %dms — assuming dead\n", i,
              waited_ms);
    }
  }

  uint32_t online = 1 + __atomic_load_n(&ap_ready_count, __ATOMIC_ACQUIRE);
  kprintf("SMP: %u CPU(s) online\n", online);
  return (int)online;
}
