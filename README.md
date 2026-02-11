# UnixOS

Minimal Unix-like operating system for i686 (32-bit x86).

## Demo

https://github.com/tlarivier/unix-os/blob/main/docs/assets/demo.mp4

> **Note**: If the video doesn't display, you can view it directly at `docs/assets/demo.mp4`

## Stats

| Metric | Value |
|--------|-------|
| Kernel C code | 6,264 lines |
| Headers | 548 lines |
| Target | i686-elf |
| Build | GCC cross-compiler |

## Features

- **Boot**: Multiboot, GDT/IDT/TSS, protected mode
- **Memory**: Paging, SLUB allocator, heap
- **Process**: fork/exec, signals, round-robin scheduler
- **Syscalls**: Linux-compatible INT 0x80 (40+ syscalls)
- **VFS**: In-memory filesystem, ELF loader
- **Drivers**: VGA text/graphics, PS/2 keyboard, PIT timer, ATA
- **IPC**: Pipes

## Build & Run

```bash
make clean && make build/bin/os.img
qemu-system-i386 -drive file=build/bin/os.img,format=raw,if=floppy
```

## Directory Structure

```
kernel/
├── arch/x86/      # GDT, TSS, context switch
├── init/          # main.c, error_handler.c
├── irq/           # IDT, interrupt handlers
├── kernel/        # scheduler, fork, exec, signals
├── mm/            # heap, paging, slub
├── drivers/       # vga, keyboard, timer, ata
├── fs/            # vfs, elf_loader
├── ipc/           # pipes
├── syscalls/      # syscall table, handlers
├── lib/           # kprintf, kstring
└── security/      # stack canary
```

## Syscall Convention

- `INT 0x80` (Linux ABI)
- EAX = syscall number
- EBX, ECX, EDX, ESI, EDI = arguments
- EAX = return value

