# UnixOS Architecture

This document describes the kernel as it stands today: an SMP (1-4 CPU)
x86-32 monolith that boots from Multiboot2, mounts ext2 from virtio-blk,
runs ring-3 userspace through a unified syscall table, and ships with a
working port of Doom.

The intent is to be the reference for *contracts* between subsystems --
who owns what state, which locks may be held at which call sites, and
what is *not allowed* to cross a given boundary. Per-file invariants are
also inscribed as a block comment at the head of every `.c`.

---

## 1. Bring-up

```
GRUB2 --(Multiboot2)--> boot/multiboot_entry.S
  -> src/kernel/init/main.c::kernel_main()
       serial_init / vga_init                       (text output)
       percpu_init_bsp / gdt_init / tss_init_bsp    (segmentation)
       paging_init / heap_init                      (mm)
       idt_init / pic_remap / pit_init              (irq + timer)
       pci_enum / virtio_blk_probe                  (block I/O)
       ramfs_init / vfs_init / ext2_mount           (filesystems)
       acpi_parse / lapic_init / ioapic_init        (SMP discovery)
       smp_start_aps                                (4-CPU bring-up)
       scheduler_init / kproc_create_idles          (sched)
       init_launch("/bin/init")                     (ring 3)
```

The BSP path lives in `src/kernel/init/main.c`. The AP trampoline is in
`src/kernel/boot/ap_trampoline.S`; once an AP lands in C it follows
`src/kernel/init/smp_bringup.c::ap_main()`. From there every CPU enters the
scheduler.

## 2. Address space layout

| Range                  | Use                              |
|------------------------|----------------------------------|
| 0x00000000 - 0x000FFFFF| BIOS/EBDA, unmapped after boot   |
| 0x00100000 - 0x003FFFFF| kernel image (.text + .rodata)   |
| 0x00400000 - 0x00BFFFFF| kernel .data + .bss + heap arena |
| 0x00C00000 - 0x00FFFFFF| early identity scratch           |
| 0x40000000 - 0x7FFFFFFF| user code, data, brk             |
| 0x80000000 - 0xBFFFFFFF| user mmap region                 |
| 0xC0000000 - 0xC03FFFFF| user stack                       |
| 0xFEC00000 / 0xFEE00000| IOAPIC / LAPIC MMIO              |

User-space sentinels: `USER_CODE_BASE = 0x40000000`, `USER_SPACE_END =
0xC0400000`, `HEAP_START = 0x60000000`. They are the single source of
truth for pointer-range checks in `sys_signal`, `sys_brk`, `sys_mmap`.

## 3. Memory management

```
src/kernel/mm/
  frame_alloc.c   bitmap + per-CPU magazine + page refcount
  page_dir.c      kernel page directory, map_page, get_physical_addr
  paging.asm      cr3 load, invlpg
  cow_temp.c      handle_cow_fault, temp_map_frame
  mmap_file.c     file-backed mmap region registry; only TU calling vfs_pread
  process_mm.c    create / destroy / clone_process_memory
  heap.c          first-fit kernel heap (kmalloc/kfree)
  slub.c          slab-style fixed-size object caches
  uaccess.c       copy_{from,to}_user with fault handling
  memory.c        memory_layout symbols, early reservations
```

Frame ownership invariants live in
`include/kernel/mm_internal.h` (private to the mm subsystem). The
heap arena and the frame allocator hold **disjoint** physical ranges --
violating this is a known footgun.

The page-fault path enters `mm/paging.c::page_fault_handler()`, which
routes to one of: COW (`handle_cow_fault`), demand-paged file mmap
(`handle_demand_fault`), demand-zero brk page, or SIGSEGV.

## 4. Process model

`process_t` (in `include/kernel/process.h`) carries:

- identity: `pid`, `ppid`, `pgid`, `sid`, `uid/gid/euid/egid/suid/sgid`
- memory: `process_memory_t *memory` (page directory + region list + brk)
- fds: `fd_table[MAX_FDS=64]` -- index into the global VFS open-file
  table; -1 = closed
- signals: `signal_handlers[NSIG_HANDLED=32]`, pending mask
- scheduling: `state`, `priority`, `quantum`, `owner_cpu`, run-queue
  links
- canary: `stack_canary` (checked at every syscall entry and exit)
- bookkeeping: `children_wq` (wait queue parents block on),
  `exit_code`, hash-table link

The global process table is a hashtable keyed by pid; entries are
embedded in `process_t`, so insert / lookup never calls `kmalloc` under
a spinlock. All edits go through `src/kernel/core/process.c` -- never
mutate `proc->state` from a syscall directly.

### Fork / exec / exit

```
sys_fork                              src/kernel/core/fork.c
  + clone_process_memory  (CoW)       src/kernel/mm/process_mm.c
  + scheduler_add_process             src/kernel/core/sched.c

sys_execve                            src/kernel/core/exec.c
  copy argv/envp into kernel buffers (BEFORE any cli)
  load_elf -> new process_memory_t
  reset CLOEXEC fds and signal_handlers
  atomic swap memory ; jump_to_usermode

process_exit                          src/kernel/core/process.c
  state = ZOMBIE ; wake_all(&parent->children_wq)
  parent's waitpid then calls process_terminate
```

## 5. Scheduling

Multi-level feedback queue with priority boosting and work-stealing.

- Run-queues are per-CPU; each CPU owns its rq_lock.
- `schedule()` picks the highest-priority runnable thread on the local
  CPU; if empty, work-steals from a peer.
- BSP idle: `kproc_create_idles` allocates an idle `process_t` per CPU
  so the scheduler never sees an empty run-queue.
- Preemption: PIT tick -> `scheduler_tick()` -> may set need_resched;
  syscall exit and IRQ return check the flag.

Critical invariants:

- `schedule()` must force a switch if `current == idle && next != NULL`
  (an early bug that hung the BSP under `-smp 4`).
- A process's `owner_cpu` is sticky; only work-stealing migrates it,
  and the stealer takes the **owner's** rq_lock.
- The lockdep facility is currently single-CPU only -- `CONFIG_LOCKDEP`
  is off in the SMP build (see `src/kernel/lib/lockdep.c`).

## 6. Filesystems

```
                       +-------------+
   syscalls (sys_fs)   |  VFS layer  |   src/kernel/fs/vfs_*.c
                       +------+------+
                              |
              +---------------+----------------+
              |               |                |
         +----+----+    +-----+-----+    +-----+-----+
         |  ramfs  |    |   ext2    |    |   pipes   |
         +---------+    +-----+-----+    +-----------+
                             |
                       +-----+-----+
                       | block_dev |   src/kernel/drivers/block/
                       +-----+-----+
                             |
                       +-----+-----+
                       | virtio-blk|
                       +-----------+
```

- `vfs_core.c` owns the open-file table + `vfs_lock`.
- `vfs_fd.c` handles `open/close/read/write/dup/dup2`.
- `vfs_path.c` handles path-based ops (`chmod/chown/mkdir/unlink/
  rename`).
- `dcache.c` caches resolved paths; invalidated on rename/unlink/mkdir.
- `ext2.c` is read-only at boot; writes are journaled through
  `journal.c` (CRC32-protected 2-slot ring; replay on next mount).
- The Doom WAD round-trips host -> ext2 -> ramfs mirror -> Doom and
  back; persistence is verified end-to-end via `debugfs` on the host.

The on-disk ext2 structs are private to `src/kernel/fs/ext2_ondisk.h` --
nothing outside `ext2.c` may include it.

## 7. Block I/O

PCI enumeration discovers a `1af4:1001` virtio device, the driver
allocates the virtqueue, maps the device's MMIO BAR, and exposes
`block_device_t` to the VFS via `src/kernel/drivers/block/block.c`.

The ext2 driver holds **per-CPU** `block_buf` scratch buffers to avoid
inter-CPU lock contention on read-paths.

Volatile discipline: virtio rings, DMA descriptor tables, and the
completion byte are accessed through `volatile` casts. Anything backed
by a shared physical page or MMIO region must follow suit.

## 8. Syscall dispatch

```
user: INT 0x80
  -> src/kernel/irq/syscall_entry.S    save regs + per-CPU gs reload
     -> syscall_handler(syscall_registers_t*)   src/kernel/syscalls/syscall_table.c
        process_check_current_canary()
        if (nr >= __NR_MAX+1 || !table[nr]) return -ENOSYS
        this_cpu()->syscall_regs = regs
        eax = table[nr](ebx, ecx, edx, esi, edi)
        process_check_current_canary()
```

The handler table is `static` and built at compile time from
`uapi/syscalls.h`. Five syscalls whose native prototype isn't
`(u32 x5) -> i32` go through tiny shims in `syscall_table.c`
(execve, pipe, getrlimit, setrlimit, nice) -- there is no
function-pointer casting in the table itself.

`sys_fork_wrap` is the one wrapper that needs the saved register frame;
it reads `this_cpu()->syscall_regs` and forwards to `sys_fork` proper.

## 9. SMP

- ACPI MADT lists the LAPIC IDs; `src/kernel/init/smp_bringup.c` IPIs each
  AP through the trampoline and waits for it to bump `cpus_online`.
- Per-CPU state (`struct per_cpu`) is reached through `%gs`. The IRQ
  entry stubs *read the LAPIC ID at every entry* to compute the GS
  selector dynamically -- this is required because LAPIC IDs are not
  contiguous (see `feedback_dynamic_gs_in_irq` for the bug that
  motivated it).
- TLB shootdown is broadcast via an IPI vector that flushes the full
  TLB on the target CPUs. We don't yet do range-tracked shootdown.

## 10. Userspace

- The dynamic linker `src/userspace/ldso/rtld.c` loads PT_DYNAMIC,
  resolves needed `DT_NEEDED` libs from `/lib`, runs relocations,
  jumps to the executable entry.
- libc is in `src/lib/libc/` -- a hand-written subset (string ops,
  printf family, malloc, syscalls, time, math, ctype). ~250 symbols.
- Init is `/bin/init`, which exec's `/bin/sh`.
- The smoke suite is `/bin/test_runner`; 30 assertions, run on every
  build.

## 11. Build & test

```
make                          builds kernel.elf, os.img, initramfs.cpio
make test                     boots os.img in QEMU (interactive)
qemu-system-i386 -kernel build/bin/kernel.elf -smp 4 \
    -nographic -serial mon:stdio
```

Smoke target = `5 x 30/30 PASS under -smp 4`. Both the integrated
test_runner and the Doom boot path are part of the routine validation
loop.

## 12. Per-file headers

Every `.c` file carries a 5-12 line English block comment at the top
stating its single responsibility, its invariants, and the things it is
*not allowed* to do. These are the authoritative micro-contracts;
when modifying a file, read its header first.
