# UnixOS

Minimal Unix-like operating system for i686 (32-bit x86). Runs symmetric
multiprocessing on up to 4 CPUs, mounts ext2 from a virtio-blk disk, and
plays Doom end-to-end on the framebuffer.

## Demo

https://github.com/tlarivier/unix-os/blob/main/docs/assets/demo.mp4

> If the embedded player doesn't load, the file is at `docs/assets/demo.mp4`.

## Stats

| Metric                  | Value         |
|-------------------------|---------------|
| Kernel C code           | 11,923 lines  |
| Kernel headers          |  3,693 lines  |
| UAPI headers            |    509 lines  |
| Userspace bins + ldso   |  2,151 lines  |
| libc (ours)             |  1,733 lines  |
| Doom port               | 62,794 lines  |
| Target triple           | i686-elf      |
| Build                   | GCC cross-compiler |

## Features

- **Boot**: Multiboot2, GDT/IDT/TSS, ring 3, ACPI MADT parsing
- **SMP**: 4-CPU bring-up via LAPIC IPI; per-CPU %gs; IOAPIC routing
- **Scheduling**: MLFQ with priority boosting and work-stealing
- **Memory**: Buddy frame allocator + magazine cache, refcounted pages,
  CoW fork, demand paging, mmap (anon + file-backed), mprotect with
  cross-CPU TLB shootdown
- **Process**: `fork`, `execve`, signals, job control (setpgid/setsid,
  tcsetpgrp), `waitpid` with wait-queues
- **Syscalls**: 65 wired, uniform `(u32 x5) -> i32` ABI via INT 0x80
- **VFS**: dcache, ramfs, ext2 mounted at `/mnt` (anything written there
  persists across reboots; writes go through a CRC32-protected ring
  journal), pipes, /dev/{console,fb,kbd}
- **Block I/O**: PCI enumeration, virtio-blk driver with completion IRQ
- **Drivers**: VGA text + 320x200 graphics, PS/2 keyboard, PIT + LAPIC
  timer, 16550 serial
- **IPC**: Pipes (anonymous, full-duplex via dup)
- **Userspace**: dynamic linker (ld.so), libc subset (~250 symbols),
  busybox-style /bin (sh, ls, cat, echo, mkdir, rm, pwd, touch,
  test_runner, hello, plasma, gfx_test)
- **Apps**: Doom (Chocolate-Doom port) -- menu, movement, fire, palette,
  WAD persistence across reboots

## Build & Run

```bash
make
qemu-system-i386 -kernel build/bin/kernel.elf -nographic \
  -serial mon:stdio -smp 4
```

For Doom with persistent WAD on virtio-blk, you need an empty ext2 image
seeded with `doom1.wad`. The shareware Doom 1 WAD is freely
redistributable -- grab it from any archive and drop it in the project
working directory before running the disk-build command. `debugfs` then
writes it into the image without needing loopback or root.

On Linux:

```bash
dd if=/dev/zero of=build/bin/disk.img bs=1M count=16
mkfs.ext2 -F -r 1 -I 256 -b 1024 -L unixos build/bin/disk.img
[ -f doom1.wad ] && echo "write doom1.wad doom1.wad" | debugfs -w -f - build/bin/disk.img
```

On macOS (mkfs.ext2 isn't native), run the same through an Alpine
container:

```bash
docker run --rm -v "$PWD:/work" -w /work alpine sh -c \
  'apk add --quiet e2fsprogs e2fsprogs-extra && \
   dd if=/dev/zero of=build/bin/disk.img bs=1M count=16 status=none && \
   mkfs.ext2 -F -r 1 -I 256 -b 1024 -L unixos build/bin/disk.img && \
   { [ -f doom1.wad ] && echo "write doom1.wad doom1.wad" | debugfs -w -f - build/bin/disk.img || true; }'
```

Then boot with the disk attached -- the kernel mounts it at `/mnt`, so
the seeded WAD lands at `/mnt/doom1.wad` where Doom looks for it. Any
file written under `/mnt` (WAD mirror, save games, etc.) persists across
reboots:

```bash
qemu-system-i386 -kernel build/bin/kernel.elf -smp 4 \
  -drive file=build/bin/disk.img,format=raw,if=virtio
```

Once the shell prompt appears, run:

```bash
doom
```

## Directory layout

```
src/kernel/
  arch/x86/   Multiboot2, GDT, IDT, TSS, LAPIC, IOAPIC, ACPI
  boot/       early bring-up + AP trampoline
  core/       process, fork, exec, sched (MLFQ), signal, jobctl, waitq
  drivers/    block (virtio), bus (PCI), input, serial, timer, tty, video
  fs/         VFS, dcache, ramfs, ext2, journal, ELF loader, cpio
  init/       main, smp bring-up, console wiring
  ipc/        pipes
  irq/        IDT setup, dispatch, syscall entry
  lib/        kprintf, kstring, hashtable, random, lockdep, KASAN-lite
  mm/         paging, frame_alloc, heap, slub, CoW, mmap, uaccess
  syscalls/   table + 65 sys_* wrappers (fs/mem/proc/time/misc)

uapi/              shared C ABI (syscalls.h, signal.h, mman.h, errno.h, ...)
include/kernel/    kernel-internal contracts (resolved via `-Iinclude`)
include/userspace/ libc/ldso headers

src/userspace/
  bin/    13 utilities + sh + test_runner
  doom/   Chocolate-Doom port (62k LOC)
  ldso/   minimal dynamic linker (rtld.c)

src/lib/libc/   our libc subset (string, stdio, malloc, syscalls, ...)
```

## Syscall ABI

```
INT 0x80
EAX  = syscall number (uapi/syscalls.h __NR_*)
EBX  = arg0   ECX = arg1   EDX = arg2
ESI  = arg3   EDI = arg4
EAX  = return value (negative = -errno)
```

Every handler in the dispatch table has the signature
`int32_t(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t)`. Syscalls whose
native prototype differs (execve, pipe, getrlimit, setrlimit, nice) go
through one-line shims in `src/kernel/syscalls/syscall_table.c` rather than
function-pointer casts. See `docs/API.md` for the full table.

## Testing

A built-in regression suite (`/bin/test_runner`) runs 30 assertions
covering syscalls, pipe semantics, fork/exec, brk/mmap, job control,
relative-path open, and libc helpers (snprintf/qsort). Smoke validation
is `5 x 30/30 PASS under -smp 4`.

## Further reading

- `docs/ARCHITECTURE.md` -- subsystem boundaries, invariants, data flow
- `docs/API.md` -- syscall reference (numbers, signatures, error codes)
