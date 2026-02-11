PROJECT_NAME := unix-os
VERSION      := 0.8.0

SRC_DIR   := .
BUILD_DIR := build
OBJ_DIR   := $(BUILD_DIR)/obj
BIN_DIR   := $(BUILD_DIR)/bin
LIB_DIR   := $(BUILD_DIR)/libs
DEPS_DIR  := $(BUILD_DIR)/deps

CC := /opt/homebrew/Cellar/i686-elf-gcc/15.2.0/bin/i686-elf-gcc
LD := /opt/homebrew/Cellar/i686-elf-binutils/2.45.1/bin/i686-elf-ld
AS := nasm
AR := /opt/homebrew/Cellar/i686-elf-binutils/2.45.1/bin/i686-elf-ar

OBJCOPY := /opt/homebrew/Cellar/i686-elf-binutils/2.45.1/bin/i686-elf-objcopy
QEMU    := qemu-system-i386

# HAVE_INITRAMFS: Use CPIO archive (preferred, Void/Linux style) - requires initramfs_data.h
# USE_EMBEDDED_BINS: Legacy fallback with embedded data headers
# Note: Add -DHAVE_INITRAMFS after running 'make initramfs' to use CPIO mode
KERNEL_CFLAGS := -std=c99 -ffreestanding -O2 -Wall -Wextra -fno-exceptions -m32 -fno-stack-protector -nostdlib -fno-builtin -Iinclude -DHAVE_INITRAMFS \
                 -fno-pic -fno-builtin -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
                 -Iinclude -I. -DKERNEL_MODE -DUSE_EMBEDDED_BINS

# Userspace compilation flags  
USER_CFLAGS := -m32 -O2 -Wall -Wextra -nostdlib \
               -fno-pic -fno-stack-protector -fno-builtin \
               -Iinclude \
               -DUSER_SPACE

# Library compilation flags (common baseline for both kernel and userspace variants)
LIB_CFLAGS := -m32 -O2 -Wall -Wextra -fno-builtin -Iinclude

USER_LDFLAGS := -m32 -nostdlib -static

LDFLAGS := -T $(SRC_DIR)/kernel/linker.ld
ASFLAGS := -f elf32
BOOT_ASFLAGS := -f bin

# Boot sources
BOOT_SOURCES := boot/boot.asm
BOOT_OBJS := $(patsubst %.asm,$(BIN_DIR)/%.bin,$(BOOT_SOURCES))

# Kernel assembly sources - separated by type
KERNEL_ARCH_ASM_SOURCES := kernel/arch/x86/entry.asm \
                           kernel/arch/x86/isr.asm \
                           kernel/arch/x86/multiboot2.asm

KERNEL_ARCH_S_SOURCES := kernel/arch/x86/gdt_flush.S \
                         kernel/arch/x86/paging_asm.S \
                         kernel/arch/x86/context_switch.S \
                         kernel/arch/x86/syscall_entry.S \
                         kernel/arch/x86/idt_flush.S \
                         kernel/arch/x86/usermode.S \
                         kernel/arch/x86/switch.S \
                         kernel/arch/x86/fork_return.S

KERNEL_ARCH_C_SOURCES := kernel/arch/x86/gdt.c \
                         kernel/arch/x86/multiboot2.c

KERNEL_INIT_SOURCES := kernel/init/main.c \
                       kernel/init/error_handler.c

KERNEL_IRQ_SOURCES := kernel/irq/interrupt.c \
                      kernel/irq/idt.c \
                      kernel/irq/irq_handler.c

KERNEL_MM_SOURCES := kernel/mm/heap.c \
                     kernel/mm/paging.c \
                     kernel/mm/slub.c \
                     kernel/mm/memory.c \
                     kernel/mm/memory_protection.c \
                     kernel/mm/uaccess.c

KERNEL_LIB_SOURCES := kernel/lib/kstring.c \
                      kernel/lib/hashtable.c \
                      kernel/lib/kprintf.c \
                      kernel/lib/random.c \
                      kernel/lib/rcu.c \
                      kernel/lib/syscall_names.c

KERNEL_CORE_SOURCES := kernel/core/fork.c \
                       kernel/core/sched.c \
                       kernel/core/signal.c \
                       kernel/core/exec.c \
                       kernel/core/limits.c \
                       kernel/core/priority.c \
                       kernel/core/usermode.c \
                       kernel/core/jobctl.c \
                       kernel/core/clone.c

KERNEL_DRIVER_SOURCES := kernel/drivers/video/vga.c \
                          kernel/drivers/video/vga_graphics.c \
                          kernel/drivers/video/framebuffer.c \
                          kernel/drivers/block/ata.c \
                          kernel/drivers/input/keyboard.c \
                          kernel/drivers/input/input_dev.c \
                          kernel/drivers/timer/pit.c \
                          kernel/drivers/tty/termios.c \
                          kernel/drivers/tty/console.c \
                          kernel/drivers/serial.c

KERNEL_FS_SOURCES := kernel/fs/vfs.c \
                     kernel/fs/ramfs.c \
                     kernel/fs/elf_loader.c \
                     kernel/fs/init_userspace.c \
                     kernel/fs/initramfs.c \
                     kernel/fs/fat12.c

KERNEL_SYSCALL_SOURCES := kernel/syscalls/syscall_table.c \
                          kernel/syscalls/sys_fs.c \
                          kernel/syscalls/sys_mem.c \
                          kernel/syscalls/sys_proc.c \
                          kernel/syscalls/sys_time.c \
                          kernel/syscalls/sys_misc.c

KERNEL_SECURITY_SOURCES := kernel/security/core_security.c \
                           kernel/security/capability.c

KERNEL_IPC_SOURCES := kernel/ipc/pipe.c \
                      kernel/ipc/shm.c \
                      kernel/ipc/sem.c \
                      kernel/ipc/futex.c

KERNEL_NET_SOURCES := kernel/net/socket.c

# Kernel tests removed - tests should be in userspace

# Kernel utility sources - ALREADY DEFINED ABOVE (line 67-68)

# Library sources (shared) - cleaned up
LIB_SOURCES := lib/syscalls/syscalls.c

# Userspace-specific sources (modular libc)
USER_LIB_SOURCES := lib/userspace/malloc.c \
                    lib/libc/errno.c \
                    lib/libc/string/strlen.c \
                    lib/libc/string/strcpy.c \
                    lib/libc/string/strncpy.c \
                    lib/libc/string/strcmp.c \
                    lib/libc/string/strncmp.c \
                    lib/libc/string/strcat.c \
                    lib/libc/string/strchr.c \
                    lib/libc/string/strrchr.c \
                    lib/libc/string/strstr.c \
                    lib/libc/string/memset.c \
                    lib/libc/string/memcpy.c \
                    lib/libc/string/memmove.c \
                    lib/libc/string/memcmp.c \
                    lib/libc/ctype/ctype.c \
                    lib/libc/stdlib/atoi.c \
                    lib/libc/stdlib/itoa.c \
                    lib/libc/stdio/putchar.c \
                    lib/libc/stdio/puts.c \
                    lib/libc/stdio/getchar.c \
                    lib/libc/stdio/printf.c

# C runtime sources
CRT_SOURCES := lib/crt/crt0.c

# Userspace sources - now built via userspace/Makefile
# All binaries in userspace/bin/

# Kernel object files by subsystem
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
KERNEL_SECURITY_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(KERNEL_SECURITY_SOURCES))
KERNEL_IPC_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(KERNEL_IPC_SOURCES))
KERNEL_NET_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(KERNEL_NET_SOURCES))

# Kernel utility object files
LIB_KERNEL_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(LIB_SOURCES))
USER_LIB_OBJECTS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(USER_LIB_SOURCES))
LIB_USER_OBJS := $(patsubst %.c,$(OBJ_DIR)/%_user.o,$(LIB_SOURCES))
CRT_OBJS := $(patsubst %.c,$(OBJ_DIR)/%_user.o,$(CRT_SOURCES))

# Userspace binaries built via userspace/Makefile

# All kernel objects
KERNEL_OBJS := $(KERNEL_ARCH_C_OBJS) $(KERNEL_ARCH_ASM_OBJS) $(KERNEL_ARCH_S_OBJS) \
               $(KERNEL_INIT_OBJS) $(KERNEL_IRQ_OBJS) $(KERNEL_MM_OBJS) \
               $(KERNEL_CORE_OBJS) $(KERNEL_DRIVER_OBJS) $(KERNEL_FS_OBJS) \
               $(KERNEL_SYSCALL_OBJS) $(KERNEL_SECURITY_OBJS) $(KERNEL_IPC_OBJS) \
               $(KERNEL_NET_OBJS) $(KERNEL_LIB_OBJS)

# Userspace built via userspace/Makefile

ALL_OBJS := $(KERNEL_OBJS) $(USER_OBJS)
DEPS := $(ALL_OBJS:.o=.d)

.PHONY: all clean test install help debug info kernel userspace libs
.SUFFIXES:

# Main targets
all: initramfs $(BIN_DIR)/kernel.elf $(BIN_DIR)/os.img

kernel: initramfs $(BIN_DIR)/kernel.elf

# Full userspace build chain:
#   1. libs    -> libc.a, crt0.o, libc.so
#   2. ldso    -> ld.so
#   3. bins    -> init, ls, cat, etc.
#   4. initramfs -> CPIO archive

# Userspace build with proper dependency tracking
# Force rebuild of sub-makefiles when sources change

LIBC_SOURCES := $(wildcard lib/libc/*/*.c) $(wildcard lib/crt/*.c) $(wildcard lib/userspace/*.c)
USERSPACE_SOURCES := $(wildcard userspace/bin/*.c)
# CRITICAL: Include ALL headers that userspace depends on
USERSPACE_HEADERS := $(wildcard userspace/include/*.h) $(wildcard uapi/*.h) $(wildcard uapi/*.def) $(wildcard include/*.h)

$(LIB_DIR)/libc.a $(LIB_DIR)/crt0.o $(LIB_DIR)/libc.so: $(LIBC_SOURCES)
	@echo "BUILD    libc.a + libc.so + crt0.o"
	@$(MAKE) -C lib all

libs: $(LIB_DIR)/libc.a

$(LIB_DIR)/ld.so: libs
	@echo "BUILD    ld.so"
	@$(MAKE) -C userspace/ldso all

ldso: $(LIB_DIR)/ld.so

# Userspace binaries depend on their sources, headers, AND libc
$(BUILD_DIR)/userspace/init $(BUILD_DIR)/userspace/sh $(BUILD_DIR)/userspace/ls: $(USERSPACE_SOURCES) $(USERSPACE_HEADERS) $(LIB_DIR)/libc.a $(LIB_DIR)/crt0.o
	@echo "BUILD    userspace binaries"
	@$(MAKE) -C userspace all

bins: $(BUILD_DIR)/userspace/init

userspace-all: libs ldso bins
	@echo "Userspace build complete"

# Create directories
$(OBJ_DIR) $(BIN_DIR) $(LIB_DIR) $(DEPS_DIR):
	@mkdir -p $@
	@mkdir -p $(DEPS_DIR)/kernel/core $(DEPS_DIR)/kernel/arch/x86 $(DEPS_DIR)/kernel/mm
	@mkdir -p $(DEPS_DIR)/kernel/proc $(DEPS_DIR)/kernel/drivers $(DEPS_DIR)/kernel/fs $(DEPS_DIR)/kernel/syscalls
	@mkdir -p $(DEPS_DIR)/lib/lib $(DEPS_DIR)/lib/syscalls
	@mkdir -p $(DEPS_DIR)/userspace/init $(DEPS_DIR)/userspace/shell $(DEPS_DIR)/userspace/test

# Kernel compilation rules
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

# Boot sector compilation
$(BIN_DIR)/%.bin: %.asm | $(BIN_DIR)
	@mkdir -p $(dir $@)
	@echo "AS-BOOT  $<"
	@$(AS) $(BOOT_ASFLAGS) $< -o $@

# Library compilation for kernel (without USER_SPACE)
$(OBJ_DIR)/lib/%.o: lib/%.c | $(OBJ_DIR) $(DEPS_DIR)
	@mkdir -p $(dir $@)
	@mkdir -p $(dir $(DEPS_DIR)/lib/$*.d)
	@echo "CC       $< (lib for kernel)"
	@$(CC) $(LIB_CFLAGS) -DKERNEL_MODE -MMD -MP -MF $(DEPS_DIR)/lib/$*.d -c $< -o $@

# Library compilation for userspace (with USER_SPACE flag)
$(OBJ_DIR)/lib/%_user.o: lib/%.c | $(OBJ_DIR) $(DEPS_DIR)
	@mkdir -p $(dir $@)
	@mkdir -p $(dir $(DEPS_DIR)/lib/$*_user.d)
	@echo "CC       $< (lib for userspace)"
	@$(CC) $(LIB_CFLAGS) -DUSER_SPACE -MMD -MP -MF $(DEPS_DIR)/lib/$*_user.d -c $< -o $@

$(LIB_DIR)/libunix.a: $(LIB_USER_OBJS) | $(LIB_DIR)
	@echo "AR       $@"
	@$(AR) rcs $@ $(LIB_USER_OBJS)

# Userspace compilation rules
$(OBJ_DIR)/userspace/%.o: userspace/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	@mkdir -p $(dir $(DEPS_DIR)/userspace/$*.d)
	@echo "CC       $< (userspace)"
	@$(CC) $(USER_CFLAGS) -MMD -MP -MF $(DEPS_DIR)/userspace/$*.d -c $< -o $@

# Kernel linking
$(BIN_DIR)/kernel.elf: $(KERNEL_OBJS) | $(BIN_DIR)
	@echo "LD       $@"
	@$(LD) $(LDFLAGS) $(KERNEL_OBJS) -o $@

# Userspace binary linking - clean architecture

# Userspace binaries built via userspace/Makefile

# Convert kernel to binary
$(BIN_DIR)/kernel.bin: $(BIN_DIR)/kernel.elf
	@echo "OBJCOPY          $@"
	@$(OBJCOPY) -O binary $< $@

# Build user/init ELF with libc.a (legacy - not used by initramfs)
$(BIN_DIR)/user_init.elf: user/init.c user/linker.ld $(LIB_DIR)/libc.a | $(BIN_DIR)
	@echo "CC+LD    user/init.c -> $@ (with libc.a)"
	@$(CC) $(USER_CFLAGS) -T user/linker.ld user/init.c $(LIB_DIR)/crt0.o $(LIB_DIR)/libc.a -o $@

FORCE:

$(BIN_DIR)/user_init.bin: $(BIN_DIR)/user_init.elf
	@echo "OBJCOPY  $@"
	@$(OBJCOPY) -O binary $< $@

# Generate C array from user binary
kernel/fs/init_elf_data.h: $(BIN_DIR)/user_init.elf
	@echo "GENERATE $@"
	@echo "/* Auto-generated from user/init.c - DO NOT EDIT */" > $@
	@echo "static const uint8_t init_elf[] = {" >> $@
	@xxd -i < $< >> $@
	@echo "};" >> $@

# Build OS image
$(BIN_DIR)/os.img: $(BIN_DIR)/boot/boot.bin $(BIN_DIR)/kernel.bin
	@echo "BUILD Image      $@"
	@dd if=/dev/zero of=$@ bs=1024 count=1440 2>/dev/null
	@dd if=$(BIN_DIR)/boot/boot.bin of=$@ bs=512 count=1 conv=notrunc 2>/dev/null
	@dd if=$(BIN_DIR)/kernel.bin of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	@echo "OS image built successfully: $@"

# Clean build
clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR)
	@echo "Clean complete"

# Test run
test: $(BIN_DIR)/os.img
	@echo "Running OS in QEMU..."
	@$(QEMU) -fda $< -monitor stdio

# Debug run
debug: $(BIN_DIR)/os.img
	@echo "Running OS in QEMU with GDB support..."
	@$(QEMU) -fda $< -s -S -monitor stdio

# Info target
info:
	@echo "Project: $(PROJECT_NAME) v$(VERSION)"
	@echo "Build directory: $(BUILD_DIR)"
	@echo "Kernel objects: $(words $(KERNEL_OBJS))"
	@echo "Userspace objects: $(words $(USER_OBJS))"

# Generate libs_data.h from shared libraries (legacy embedded mode)
libs-data: ldso libs
	@echo "Generating kernel/fs/libs_data.h..."
	@echo "/* libs_data.h - Auto-generated */" > kernel/fs/libs_data.h
	@echo "#define HAVE_LDSO" >> kernel/fs/libs_data.h
	@echo "#define HAVE_LIBC_SO" >> kernel/fs/libs_data.h
	@echo "static const uint8_t lib_ldso[] = {" >> kernel/fs/libs_data.h
	@xxd -i < $(LIB_DIR)/ld.so >> kernel/fs/libs_data.h
	@echo "};" >> kernel/fs/libs_data.h
	@echo "static const uint8_t lib_libc_so[] = {" >> kernel/fs/libs_data.h
	@xxd -i < $(LIB_DIR)/libc.so >> kernel/fs/libs_data.h
	@echo "};" >> kernel/fs/libs_data.h

# Build initramfs CPIO archive (Void/Linux style - preferred method)
# Requires: make userspace-all first
initramfs: userspace-all
	@echo "BUILD    initramfs.cpio"
	@mkdir -p $(BUILD_DIR)/initramfs/bin
	@mkdir -p $(BUILD_DIR)/initramfs/sbin
	@mkdir -p $(BUILD_DIR)/initramfs/lib
	@mkdir -p $(BUILD_DIR)/initramfs/dev
	@mkdir -p $(BUILD_DIR)/initramfs/tmp
	@# Copy ld.so and libc.so
	@test -f $(LIB_DIR)/ld.so && cp $(LIB_DIR)/ld.so $(BUILD_DIR)/initramfs/lib/ || echo "WARN: ld.so not found"
	@test -f $(LIB_DIR)/libc.so && cp $(LIB_DIR)/libc.so $(BUILD_DIR)/initramfs/lib/ || echo "WARN: libc.so not found"
	@# Copy userspace binaries
	@test -f $(BUILD_DIR)/userspace/init && cp $(BUILD_DIR)/userspace/init $(BUILD_DIR)/initramfs/sbin/ || true
	@for bin in ls cat mkdir rm pwd echo touch sh hotreload_test; do \
		test -f $(BUILD_DIR)/userspace/$$bin && cp $(BUILD_DIR)/userspace/$$bin $(BUILD_DIR)/initramfs/bin/ || true; \
	done
	@# Copy modules (.so files)
	@test -f $(BUILD_DIR)/modules/testmod.so && cp $(BUILD_DIR)/modules/testmod.so $(BUILD_DIR)/initramfs/lib/ || true
	@# Create CPIO archive
	@mkdir -p $(BIN_DIR)
	@cd $(BUILD_DIR)/initramfs && find . | cpio -o -H newc > ../bin/initramfs.cpio 2>/dev/null
	@echo "Initramfs: $$(cd $(BUILD_DIR)/initramfs && find . -type f | wc -l | tr -d ' ') files -> $(BIN_DIR)/initramfs.cpio"

# Generate initramfs_data.h from CPIO (only when needed for embedded mode)
kernel/fs/initramfs_data.h: $(BIN_DIR)/initramfs.cpio
	@echo "GENERATE $@ (for embedded mode)"
	@echo "/* Auto-generated from initramfs.cpio */" > $@
	@echo "const unsigned char initramfs_data[] = {" >> $@
	@xxd -i < $< >> $@
	@echo "};" >> $@
	@echo "const unsigned int initramfs_size = sizeof(initramfs_data);" >> $@

# CRITICAL: initramfs.o must be recompiled when initramfs_data.h changes
$(OBJ_DIR)/kernel/fs/initramfs.o: kernel/fs/initramfs_data.h

# CRITICAL: kernel.elf depends on initramfs_data.h (embedded initramfs)
$(BIN_DIR)/kernel.elf: kernel/fs/initramfs_data.h

# Create bootable ISO with GRUB2 (for Multiboot2 boot)
iso: initramfs kernel/fs/initramfs_data.h $(BIN_DIR)/kernel.elf
	@echo "BUILD    ISO with GRUB2"
	@mkdir -p $(BUILD_DIR)/iso/boot/grub
	@cp $(BIN_DIR)/kernel.elf $(BUILD_DIR)/iso/boot/
	@cp $(BIN_DIR)/initramfs.cpio $(BUILD_DIR)/iso/boot/
	@cp boot/grub.cfg $(BUILD_DIR)/iso/boot/grub/
	@i686-elf-grub-mkrescue -o $(BUILD_DIR)/unixos.iso $(BUILD_DIR)/iso 2>/dev/null || \
		grub-mkrescue -o $(BUILD_DIR)/unixos.iso $(BUILD_DIR)/iso 2>/dev/null || \
		echo "Note: grub-mkrescue not found. Install grub2 tools to create ISO."
	@test -f $(BUILD_DIR)/unixos.iso && echo "ISO created: $(BUILD_DIR)/unixos.iso" || true

# Run ISO in QEMU
run-iso: iso
	qemu-system-i386 -cdrom $(BUILD_DIR)/unixos.iso -m 64M

# Help target
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
