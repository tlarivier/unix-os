/*
 * main.c — orchestrate kernel boot via kmain(), sequencing subsystem
 * init entry-points from CPU basics up to /sbin/init in ring 3.
 *
 * Invariants:
 *  - kmain runs exactly once, on the BSP, and never returns (terminal halt).
 *  - Boot order is strict: serial -> vga -> percpu -> gdt -> kprintf-lock ->
 *    tss -> idt -> keyboard -> timer -> error -> acpi -> paging -> lapic ->
 *    ioapic -> heap -> process -> scheduler -> APs -> block/journal ->
 *    vfs/userspace -> idle -> enable scheduler -> ring3.
 *  - kprintf must not take its spinlock before kprintf_enable_locking().
 *  - Phase helpers (init_cpu_basics, init_block_and_journal, ...) are
 *    static; no phase is exposed as a public API.
 *
 * Not allowed:
 *  - Return from kmain or expose any phase helper in a header.
 *  - Reference "current" state via globals outside cpu_t (current_proc, ...).
 *  - Treat init returning from userspace as recoverable (it is a panic).
 */

#include <kernel/acpi.h>
#include <kernel/block.h>
#include <kernel/errno.h>
#include <kernel/ext2.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/initramfs.h>
#include <kernel/ioapic.h>
#include <kernel/journal.h>
#include <kernel/kernel.h>
#include <kernel/keyboard.h>
#include <kernel/kprintf.h>
#include <kernel/lapic.h>
#include <kernel/memory.h>
#include <kernel/pci.h>
#include <kernel/percpu.h>
#include <kernel/pipe.h>
#include <kernel/pit.h>
#include <kernel/process.h>
#include <kernel/random.h>
#include <kernel/rcu.h>
#include <kernel/scheduler.h>
#include <kernel/serial.h>
#include <kernel/smp.h>
#include <kernel/timer.h>
#include <kernel/vfs.h>
#include <kernel/vfs_extra.h>
#include <kernel/vga.h>
#include <kernel/virtio_blk.h>

/* Boot phase tracker — incremented monotonically. Each phase asserts the
 * previous step ran, so reordering kmain() turns a confusing runtime crash
 * into a clear panic at the offending phase. */
typedef enum {
  BOOT_PHASE_NONE = 0,
  BOOT_PHASE_CPU,        /* gdt/idt/tss/percpu */
  BOOT_PHASE_ACPI_SMP,   /* topology known */
  BOOT_PHASE_PAGING,     /* heap usable */
  BOOT_PHASE_PROCESS,    /* process_t alloc'able */
  BOOT_PHASE_APS,        /* SMP up */
  BOOT_PHASE_BLOCK,      /* virtio-blk + journal */
  BOOT_PHASE_VFS,        /* mounts done */
  BOOT_PHASE_SCHED,      /* scheduler running */
} boot_phase_t;

static boot_phase_t g_boot_phase = BOOT_PHASE_NONE;

static void boot_phase_advance(boot_phase_t expected_prev, boot_phase_t next) {
  if (g_boot_phase != expected_prev) {
    KERNEL_PANIC("boot phase ordering violated");
  }
  g_boot_phase = next;
}

void check_stack_canary(void) { process_check_current_canary(); }

static void init_cpu_basics(void) {
  serial_init();
  random_init();
  vga_init();
  kprintf("Unix-like OS Booting...\n");

  percpu_init_bsp();
  gdt_init();
  kprintf_enable_locking();
  tss_init_bsp();
  idt_init();
  keyboard_init();
  timer_init(100);
  check_stack_canary();
}

static void init_block_and_journal(void) {
  pci_init();
  (void)virtio_blk_init();

  block_device_t *bd = block_device_find("vda");
  if (bd && ext2_mount(bd) == 0) {
    ext2_print_info();
  }

  if (!bd)
    return;

  uint64_t cap_sectors = virtio_blk_capacity();
  if (cap_sectors <= (JOURNAL_DEFAULT_BLOCKS + 1) * 8)
    return;

  uint32_t journal_start_block =
      (uint32_t)(cap_sectors / 8) - JOURNAL_DEFAULT_BLOCKS;

  if (journal_init(bd, journal_start_block) != 0) {
    kprintf("Journal: init failed\n");
    return;
  }
  journal_replay();

  if (ext2_is_mounted()) {
    int n = ext2_reserve_blocks(journal_start_block, JOURNAL_DEFAULT_BLOCKS);
    if (n > 0) {
      kprintf("Journal: reserved %d ext2 blocks for journal area\n", n);
    }
  }
}

static void init_vfs_and_userspace(void) {
  vfs_init();

  vfs_mkdir("/sbin", 0755);
  vfs_mkdir("/bin", 0755);
  vfs_mkdir("/dev", 0755);
  vfs_mkdir("/tmp", 0777);
  vfs_mkdir("/mnt", 0755);

  int mrc = vfs_mount_ext2_at("/mnt");
  if (mrc == 0)
    kprintf("VFS: ext2 mounted at /mnt\n");
  else if (mrc != -ENODEV)
    kprintf("VFS: ext2 mount at /mnt failed: %d\n", mrc);

  install_userspace_binaries();

  pipe_subsystem_init();
}

static void init_scheduler_run(void) {
  process_t *kproc = get_current_process();
  if (kproc) {
    scheduler_add_process(kproc);
  }

  process_t *bsp_idle = process_create("idle-cpu0", (void *)ap_idle_loop);
  if (bsp_idle)
    bsp_idle->owner_cpu = 0;
  scheduler_register_idle(0, bsp_idle);

  kprintf("RCU: synchronize_rcu pre-scheduler...\n");
  synchronize_rcu();
  kprintf("RCU: synchronize_rcu OK\n");

  scheduler_enable();
}

void kmain(void) {
  init_cpu_basics();
  boot_phase_advance(BOOT_PHASE_NONE, BOOT_PHASE_CPU);

  acpi_init();
  smp_init();
  boot_phase_advance(BOOT_PHASE_CPU, BOOT_PHASE_ACPI_SMP);

  paging_init();
  lapic_init();
  ioapic_init();
  memory_init();
  boot_phase_advance(BOOT_PHASE_ACPI_SMP, BOOT_PHASE_PAGING);

  process_init();
  boot_phase_advance(BOOT_PHASE_PAGING, BOOT_PHASE_PROCESS);

  g_lapic_timer_initial = lapic_timer_calibrate();
  kprintf("LAPIC timer: calibrated %u ticks / 10 ms\n", g_lapic_timer_initial);
  smp_bring_up_aps();
  boot_phase_advance(BOOT_PHASE_PROCESS, BOOT_PHASE_APS);

  init_block_and_journal();
  boot_phase_advance(BOOT_PHASE_APS, BOOT_PHASE_BLOCK);

  init_vfs_and_userspace();
  boot_phase_advance(BOOT_PHASE_BLOCK, BOOT_PHASE_VFS);

  init_scheduler_run();
  boot_phase_advance(BOOT_PHASE_VFS, BOOT_PHASE_SCHED);

  start_init_process();
  KERNEL_PANIC(
      "init returned from userspace; this is impossible by Unix contract");
}
