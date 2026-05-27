PROJECT_NAME := unix-os
VERSION      := 0.8.0

SRC_DIR   := .
BUILD_DIR := build
OBJ_DIR   := $(BUILD_DIR)/obj
BIN_DIR   := $(BUILD_DIR)/bin
LIB_DIR   := $(BUILD_DIR)/libs
DEPS_DIR  := $(BUILD_DIR)/deps

CC := /opt/homebrew/bin/i686-elf-gcc
LD := /opt/homebrew/bin/i686-elf-ld
AS := nasm
AR := /opt/homebrew/bin/i686-elf-ar

OBJCOPY := /opt/homebrew/bin/i686-elf-objcopy
QEMU    := qemu-system-i386

KERNEL_CFLAGS := -std=c99 -ffreestanding -O2 -Wall -Werror -Wextra -fno-exceptions -m32 -fno-stack-protector -nostdlib -fno-builtin -Iinclude -DHAVE_INITRAMFS \
                 -fno-pic -fno-builtin -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
                 -Iinclude -I. -DKERNEL_MODE -DUSE_EMBEDDED_BINS \
                 -DCONFIG_KASAN_LITE

USER_CFLAGS := -m32 -O2 -Wall -Wextra -nostdlib \
               -fno-pic -fno-stack-protector -fno-builtin \
               -Iinclude \
               -DUSER_SPACE

LIB_CFLAGS := -m32 -O2 -Wall -Wextra -fno-builtin -Iinclude

USER_LDFLAGS := -m32 -nostdlib -static

LDFLAGS := -T $(SRC_DIR)/src/kernel/linker.ld
ASFLAGS := -f elf32
BOOT_ASFLAGS := -f bin

BOOT_SOURCES := boot/boot.asm
BOOT_OBJS := $(patsubst %.asm,$(BIN_DIR)/%.bin,$(BOOT_SOURCES))

KERNEL_ARCH_ASM_SOURCES := src/kernel/arch/x86/entry.asm \
                           src/kernel/arch/x86/isr.asm \
                           src/kernel/arch/x86/multiboot2.asm

KERNEL_ARCH_S_SOURCES := src/kernel/arch/x86/gdt_flush.S \
                         src/kernel/arch/x86/paging_asm.S \
                         src/kernel/arch/x86/syscall_entry.S \
                         src/kernel/arch/x86/idt_flush.S \
                         src/kernel/arch/x86/usermode.S \
                         src/kernel/arch/x86/switch.S \
                         src/kernel/arch/x86/fork_return.S

KERNEL_ARCH_C_SOURCES := src/kernel/arch/x86/gdt.c \
                         src/kernel/arch/x86/multiboot2.c \
                         src/kernel/arch/x86/acpi.c \
                         src/kernel/arch/x86/lapic.c \
                         src/kernel/arch/x86/lapic_calibrate.c \
                         src/kernel/arch/x86/ioapic.c

KERNEL_INIT_SOURCES := src/kernel/init/main.c \
                       src/kernel/init/error_handler.c \
                       src/kernel/init/percpu.c \
                       src/kernel/init/smp.c \
                       src/kernel/init/smp_bringup.c

KERNEL_IRQ_SOURCES := src/kernel/irq/interrupt.c \
                      src/kernel/irq/idt.c

KERNEL_MM_SOURCES := src/kernel/mm/heap.c \
                     src/kernel/mm/frame_alloc.c \
                     src/kernel/mm/page_dir.c \
                     src/kernel/mm/cow_temp.c \
                     src/kernel/mm/mmap_file.c \
                     src/kernel/mm/process_mm.c \
                     src/kernel/mm/slub.c \
                     src/kernel/mm/memory.c \
                     src/kernel/mm/uaccess.c

KERNEL_LIB_SOURCES := src/kernel/lib/kstring.c \
                      src/kernel/lib/hashtable.c \
                      src/kernel/lib/kprintf.c \
                      src/kernel/lib/random.c \
                      src/kernel/lib/rcu.c \
                      src/kernel/lib/lockdep.c \
                      src/kernel/lib/kasan.c

KERNEL_CORE_SOURCES := src/kernel/core/process.c \
                       src/kernel/core/fork.c \
                       src/kernel/core/stack_canary.c \
                       src/kernel/core/sched.c \
                       src/kernel/core/signal.c \
                       src/kernel/core/exec.c \
                       src/kernel/core/cred.c \
                       src/kernel/core/limits.c \
                       src/kernel/core/priority.c \
                       src/kernel/core/init_launch.c \
                       src/kernel/core/jobctl.c \
                       src/kernel/core/waitq.c \
                       src/kernel/core/alarm.c \
                       src/kernel/core/timer.c \
                       src/kernel/core/ipi.c

KERNEL_DRIVER_SOURCES := src/kernel/drivers/video/vga.c \
                          src/kernel/drivers/video/vga_graphics.c \
                          src/kernel/drivers/input/keyboard.c \
                          src/kernel/drivers/timer/pit.c \
                          src/kernel/drivers/tty/termios.c \
                          src/kernel/drivers/tty/console.c \
                          src/kernel/drivers/serial.c \
                          src/kernel/drivers/bus/pci.c \
                          src/kernel/drivers/block/block.c \
                          src/kernel/drivers/block/virtio_blk.c

KERNEL_FS_SOURCES := src/kernel/fs/vfs_core.c \
                     src/kernel/fs/vfs_fd.c \
                     src/kernel/fs/vfs_path.c \
                     src/kernel/fs/vfs_mount.c \
                     src/kernel/fs/ramfs.c \
                     src/kernel/fs/ramfs_ops.c \
                     src/kernel/fs/elf_loader.c \
                     src/kernel/fs/cpio_parse.c \
                     src/kernel/fs/initramfs.c \
                     src/kernel/fs/ext2.c \
                     src/kernel/fs/ext2_ops.c \
                     src/kernel/fs/journal.c \
                     src/kernel/fs/dcache.c

KERNEL_SYSCALL_SOURCES := src/kernel/syscalls/syscall_table.c \
                          src/kernel/syscalls/sys_fs.c \
                          src/kernel/syscalls/sys_mem.c \
                          src/kernel/syscalls/sys_proc.c \
                          src/kernel/syscalls/sys_time.c \
                          src/kernel/syscalls/sys_misc.c

KERNEL_IPC_SOURCES := src/kernel/ipc/pipe.c


KERNEL_ARCH_ASM_OBJS := $(patsubst %.asm,$(OBJ_DIR)/%_asm.o,$(KERNEL_ARCH_ASM_SOURCES))
KERNEL_ARCH_S_OBJS := $(patsubst %.S,$(OBJ_DIR)/%_asm.o,$(KERNEL_ARCH_S_SOURCES))
KERNEL_ARCH_C_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(KERNEL_ARCH_C_SOURCES))
KERNEL_INIT_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(KERNEL_INIT_SOURCES))
KERNEL_IRQ_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(KERNEL_IRQ_SOURCES))
KERNEL_LIB_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(KERNEL_LIB_SOURCES))
KERNEL_MM_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(KERNEL_MM_SOURCES))
KERNEL_CORE_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(KERNEL_CORE_SOURCES))
KERNEL_DRIVER_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(KERNEL_DRIVER_SOURCES))
KERNEL_FS_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(KERNEL_FS_SOURCES))
KERNEL_SYSCALL_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(KERNEL_SYSCALL_SOURCES))
KERNEL_IPC_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(KERNEL_IPC_SOURCES))

KERNEL_OBJS := $(KERNEL_ARCH_C_OBJS) $(KERNEL_ARCH_ASM_OBJS) $(KERNEL_ARCH_S_OBJS) \
               $(KERNEL_INIT_OBJS) $(KERNEL_IRQ_OBJS) $(KERNEL_MM_OBJS) \
               $(KERNEL_CORE_OBJS) $(KERNEL_DRIVER_OBJS) $(KERNEL_FS_OBJS) \
               $(KERNEL_SYSCALL_OBJS) $(KERNEL_IPC_OBJS) \
               $(KERNEL_LIB_OBJS)

ALL_OBJS := $(KERNEL_OBJS) $(USER_OBJS)
DEPS := $(ALL_OBJS:.o=.d)

.PHONY: all clean test install help debug info kernel userspace libs
.SUFFIXES:

all: initramfs $(BIN_DIR)/kernel.elf $(BIN_DIR)/os.img

kernel: initramfs $(BIN_DIR)/kernel.elf

LIBC_SOURCES := $(wildcard src/lib/libc/*/*.c) $(wildcard src/lib/crt/*.c)
USERSPACE_SOURCES := $(wildcard src/userspace/bin/*.c)
USERSPACE_HEADERS := $(wildcard src/userspace/include/*.h) $(wildcard uapi/*.h) $(wildcard uapi/*.def) $(wildcard include/*.h)

$(LIB_DIR)/libc.a $(LIB_DIR)/crt0.o $(LIB_DIR)/libc.so: $(LIBC_SOURCES)
	@echo "BUILD    libc.a + libc.so + crt0.o"
	+@$(MAKE) -C src/lib all

libs: $(LIB_DIR)/libc.a

$(LIB_DIR)/ld.so: libs
	@echo "BUILD    ld.so"
	+@$(MAKE) -C src/userspace/ldso all

ldso: $(LIB_DIR)/ld.so

DOOM_SOURCES := $(wildcard src/userspace/doom/doomgeneric/*.c)

$(BUILD_DIR)/userspace/init $(BUILD_DIR)/userspace/sh $(BUILD_DIR)/userspace/ls $(BUILD_DIR)/userspace/doom: $(USERSPACE_SOURCES) $(USERSPACE_HEADERS) $(DOOM_SOURCES) $(LIB_DIR)/libc.a $(LIB_DIR)/crt0.o
	@echo "BUILD    userspace binaries"
	+@$(MAKE) -C src/userspace all

DOOM_DEP := $(if $(wildcard src/userspace/doom/doomgeneric/doomgeneric.c),$(BUILD_DIR)/userspace/doom)
bins: $(BUILD_DIR)/userspace/init $(DOOM_DEP)

userspace-all: libs ldso bins
	@echo "Userspace build complete"

$(OBJ_DIR) $(BIN_DIR) $(LIB_DIR) $(DEPS_DIR):
	@mkdir -p $@
	@mkdir -p $(DEPS_DIR)/kernel/core $(DEPS_DIR)/src/kernel/arch/x86 $(DEPS_DIR)/kernel/mm
	@mkdir -p $(DEPS_DIR)/kernel/proc $(DEPS_DIR)/kernel/drivers $(DEPS_DIR)/kernel/fs $(DEPS_DIR)/kernel/syscalls
	@mkdir -p $(DEPS_DIR)/lib/lib $(DEPS_DIR)/lib/syscalls
	@mkdir -p $(DEPS_DIR)/userspace/init $(DEPS_DIR)/userspace/shell $(DEPS_DIR)/userspace/test

$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	@mkdir -p $(dir $(DEPS_DIR)/$*.d)
	@echo "CC       $<"
	@$(CC) $(KERNEL_CFLAGS) -MMD -MP -MF $(DEPS_DIR)/$*.d -c $< -o $@

$(OBJ_DIR)/%_asm.o: %.asm | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	@echo "AS       $<"
	@$(AS) $(ASFLAGS) $< -o $@

$(OBJ_DIR)/%_asm.o: %.S | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	@echo "AS       $<"
	@$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BIN_DIR)/%.bin: %.asm | $(BIN_DIR)
	@mkdir -p $(dir $@)
	@echo "AS-BOOT  $<"
	@$(AS) $(BOOT_ASFLAGS) $< -o $@

$(OBJ_DIR)/lib/%.o: src/lib/%.c | $(OBJ_DIR) $(DEPS_DIR)
	@mkdir -p $(dir $@)
	@mkdir -p $(dir $(DEPS_DIR)/lib/$*.d)
	@echo "CC       $< (lib for kernel)"
	@$(CC) $(LIB_CFLAGS) -DKERNEL_MODE -MMD -MP -MF $(DEPS_DIR)/lib/$*.d -c $< -o $@

$(OBJ_DIR)/lib/%_user.o: src/lib/%.c | $(OBJ_DIR) $(DEPS_DIR)
	@mkdir -p $(dir $@)
	@mkdir -p $(dir $(DEPS_DIR)/lib/$*_user.d)
	@echo "CC       $< (lib for userspace)"
	@$(CC) $(LIB_CFLAGS) -DUSER_SPACE -MMD -MP -MF $(DEPS_DIR)/lib/$*_user.d -c $< -o $@

$(OBJ_DIR)/userspace/%.o: src/userspace/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	@mkdir -p $(dir $(DEPS_DIR)/userspace/$*.d)
	@echo "CC       $< (userspace)"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(DEPS_DIR)/userspace/$*.d -c $< -o $@

$(BIN_DIR)/kernel.elf: $(KERNEL_OBJS) | $(BIN_DIR)
	@echo "LD       $@"
	@$(LD) $(LDFLAGS) $(KERNEL_OBJS) -o $@

$(BIN_DIR)/kernel.bin: $(BIN_DIR)/kernel.elf
	@echo "OBJCOPY          $@"
	@$(OBJCOPY) -O binary $< $@

$(BIN_DIR)/user_init.elf: user/init.c user/linker.ld $(LIB_DIR)/libc.a | $(BIN_DIR)
	@echo "CC+LD    user/init.c -> $@ (with libc.a)"
	@$(CC) $(USER_CFLAGS) -T user/linker.ld user/init.c $(LIB_DIR)/crt0.o $(LIB_DIR)/libc.a -o $@

FORCE:

$(BIN_DIR)/user_init.bin: $(BIN_DIR)/user_init.elf
	@echo "OBJCOPY  $@"
	@$(OBJCOPY) -O binary $< $@

src/kernel/fs/init_elf_data.h: $(BIN_DIR)/user_init.elf
	@echo "GENERATE $@"
	@echo "/* Auto-generated from user/init.c - DO NOT EDIT */" > $@
	@echo "static const uint8_t init_elf[] = {" >> $@
	@xxd -i < $< >> $@
	@echo "};" >> $@

$(BIN_DIR)/os.img: $(BIN_DIR)/boot/boot.bin $(BIN_DIR)/kernel.bin
	@echo "BUILD Image      $@"
	@dd if=/dev/zero of=$@ bs=1024 count=1440 2>/dev/null
	@dd if=$(BIN_DIR)/boot/boot.bin of=$@ bs=512 count=1 conv=notrunc 2>/dev/null
	@dd if=$(BIN_DIR)/kernel.bin of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	@echo "OS image built successfully: $@"

clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR)
	@echo "Clean complete"

test: $(BIN_DIR)/os.img
	@echo "Running OS in QEMU..."
	@$(QEMU) -fda $< -monitor stdio

debug: $(BIN_DIR)/os.img
	@echo "Running OS in QEMU with GDB support..."
	@$(QEMU) -fda $< -s -S -monitor stdio

info:
	@echo "Project: $(PROJECT_NAME) v$(VERSION)"
	@echo "Build directory: $(BUILD_DIR)"
	@echo "Kernel objects: $(words $(KERNEL_OBJS))"
	@echo "Userspace objects: $(words $(USER_OBJS))"

libs-data: ldso libs
	@echo "Generating src/kernel/fs/libs_data.h..."
	@echo "/* libs_data.h - Auto-generated */" > src/kernel/fs/libs_data.h
	@echo "#define HAVE_LDSO" >> src/kernel/fs/libs_data.h
	@echo "#define HAVE_LIBC_SO" >> src/kernel/fs/libs_data.h
	@echo "static const uint8_t lib_ldso[] = {" >> src/kernel/fs/libs_data.h
	@xxd -i < $(LIB_DIR)/ld.so >> src/kernel/fs/libs_data.h
	@echo "};" >> src/kernel/fs/libs_data.h
	@echo "static const uint8_t lib_libc_so[] = {" >> src/kernel/fs/libs_data.h
	@xxd -i < $(LIB_DIR)/libc.so >> src/kernel/fs/libs_data.h
	@echo "};" >> src/kernel/fs/libs_data.h

$(BIN_DIR)/initramfs.cpio: userspace-all
	@echo "BUILD    initramfs.cpio"
	@mkdir -p $(BUILD_DIR)/initramfs/bin
	@mkdir -p $(BUILD_DIR)/initramfs/sbin
	@mkdir -p $(BUILD_DIR)/initramfs/lib
	@mkdir -p $(BUILD_DIR)/initramfs/dev
	@mkdir -p $(BUILD_DIR)/initramfs/tmp
	@test -f $(LIB_DIR)/ld.so && cp $(LIB_DIR)/ld.so $(BUILD_DIR)/initramfs/lib/ || echo "WARN: ld.so not found"
	@test -f $(LIB_DIR)/libc.so && cp $(LIB_DIR)/libc.so $(BUILD_DIR)/initramfs/lib/ || echo "WARN: libc.so not found"
	@for f in $(BUILD_DIR)/userspace/*; do \
		[ -f "$$f" ] || continue; \
		case "$$f" in *.o|*.s|*.cpio|*.h) continue;; esac; \
		head -c 4 "$$f" 2>/dev/null | od -An -tx1 | tr -d ' \n' | grep -q '7f454c46' || continue; \
		name=$$(basename "$$f"); \
		case "$$name" in \
			init|test_runner) cp "$$f" $(BUILD_DIR)/initramfs/sbin/ ;; \
			*)                cp "$$f" $(BUILD_DIR)/initramfs/bin/  ;; \
		esac; \
	done
	@mkdir -p $(BIN_DIR)
	@cd $(BUILD_DIR)/initramfs && find . | cpio -o -H newc > ../bin/initramfs.cpio 2>/dev/null
	@echo "Initramfs: $$(cd $(BUILD_DIR)/initramfs && find . -type f | wc -l | tr -d ' ') files -> $(BIN_DIR)/initramfs.cpio"

initramfs: $(BIN_DIR)/initramfs.cpio

src/kernel/fs/initramfs_data.h: $(BIN_DIR)/initramfs.cpio
	@echo "GENERATE $@ (for embedded mode)"
	@echo "/* Auto-generated from initramfs.cpio */" > $@
	@echo "const unsigned char initramfs_data[] = {" >> $@
	@xxd -i < $< >> $@
	@echo "};" >> $@
	@echo "const unsigned int initramfs_size = sizeof(initramfs_data);" >> $@

$(OBJ_DIR)/src/kernel/fs/cpio_parse.o: src/kernel/fs/initramfs_data.h

$(BIN_DIR)/kernel.elf: src/kernel/fs/initramfs_data.h

iso: initramfs src/kernel/fs/initramfs_data.h $(BIN_DIR)/kernel.elf
	@echo "BUILD    ISO with GRUB2"
	@mkdir -p $(BUILD_DIR)/iso/boot/grub
	@cp $(BIN_DIR)/kernel.elf $(BUILD_DIR)/iso/boot/
	@cp $(BIN_DIR)/initramfs.cpio $(BUILD_DIR)/iso/boot/
	@cp boot/grub.cfg $(BUILD_DIR)/iso/boot/grub/
	@i686-elf-grub-mkrescue -o $(BUILD_DIR)/unixos.iso $(BUILD_DIR)/iso 2>/dev/null || \
		grub-mkrescue -o $(BUILD_DIR)/unixos.iso $(BUILD_DIR)/iso 2>/dev/null || \
		echo "Note: grub-mkrescue not found. Install grub2 tools to create ISO."
	@test -f $(BUILD_DIR)/unixos.iso && echo "ISO created: $(BUILD_DIR)/unixos.iso" || true

run-iso: iso
	qemu-system-i386 -cdrom $(BUILD_DIR)/unixos.iso -m 64M

help:
	@echo "UnixOS Build System"
	@echo ""
	@echo "Kernel:"
	@echo "  make kernel       - Build kernel only"
	@echo "  make              - Same as kernel"
	@echo ""
	@echo "Userspace (build chain):"
	@echo "  make libs         - 1. Build libc.a, libc.so, crt0.o"
	@echo "  make ldso         - 2. Build ld.so (requires libs)"
	@echo "  make bins         - 3. Build userspace binaries (requires libs)"
	@echo "  make userspace-all- Full userspace build (libs+ldso+bins)"
	@echo "  make initramfs    - Build CPIO archive (requires userspace-all)"
	@echo ""
	@echo "Legacy (embedded mode):"
	@echo "  make libs-data    - Generate libs_data.h (requires ldso+libs)"
	@echo ""
	@echo "Run:"
	@echo "  make test         - Run OS in QEMU (floppy image)"
	@echo "  make debug        - Run OS in QEMU with GDB"
	@echo "  make iso          - Create bootable ISO (GRUB2/Multiboot2)"
	@echo "  make run-iso      - Run ISO in QEMU"
	@echo ""
	@echo "Other:"
	@echo "  make clean        - Clean build directory"
	@echo "  make info         - Show build information"

# Include dependency files
-include $(DEPS)
