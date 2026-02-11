# Makefile for userspace binaries

CC := /opt/homebrew/Cellar/i686-elf-gcc/15.2.0/bin/i686-elf-gcc
LD := /opt/homebrew/Cellar/i686-elf-binutils/2.45.1/bin/i686-elf-ld

CFLAGS := -m32 -O2 -Wall -nostdlib -fno-builtin -fno-pic -fno-stack-protector -ffreestanding \
          -I../lib/include -I../include -I../uapi
LDFLAGS := -m elf_i386 -T linker.ld

BUILD_DIR := ../build/userspace
LIBC := ../build/libs/libc.a
CRT0 := ../build/libs/crt0.o

# Static binaries (core utilities)
STATIC_BINS := init sh

# Dynamic binaries (linked against libc.so via ld.so)
DYNAMIC_BINS := ls mkdir cat rm pwd echo touch test_read

# Header dependencies - recompile when ANY header changes
HEADERS := $(wildcard include/*.h) $(wildcard ../include/*.h) $(wildcard ../uapi/*.h) $(wildcard ../uapi/*.def)

# Shared libs
LIBC_SO := ../build/libs/libc.so
LDSO := ../build/libs/ld.so

all: dirs $(addprefix $(BUILD_DIR)/,$(STATIC_BINS)) $(addprefix $(BUILD_DIR)/,$(DYNAMIC_BINS))

dirs:
	@mkdir -p $(BUILD_DIR)

# Static binaries depend on their source, libc, crt0, AND all headers
$(BUILD_DIR)/%: bin/%.c $(CRT0) $(LIBC) $(HEADERS)
	@echo "CC+LD    $< -> $@"
	@$(CC) $(CFLAGS) -c $< -o $(BUILD_DIR)/$*.o
	@$(LD) $(LDFLAGS) -o $@ $(CRT0) $(BUILD_DIR)/$*.o $(LIBC)

# Dynamic binaries - linked with PT_INTERP to use ld.so
LDFLAGS_DYN := -m elf_i386 -T linker_dynamic.ld --dynamic-linker=/lib/ld.so
$(BUILD_DIR)/interp.o: 
	@echo "GEN      interp.o"
	@echo '.section .interp,"a",@progbits' > $(BUILD_DIR)/interp.s
	@echo '.asciz "/lib/ld.so"' >> $(BUILD_DIR)/interp.s
	@$(CC) -m32 -c $(BUILD_DIR)/interp.s -o $@

# Pattern rule for dynamic binaries
$(BUILD_DIR)/ls $(BUILD_DIR)/mkdir $(BUILD_DIR)/cat $(BUILD_DIR)/rm $(BUILD_DIR)/pwd $(BUILD_DIR)/echo $(BUILD_DIR)/touch $(BUILD_DIR)/test_read: $(BUILD_DIR)/%: bin/%.c $(CRT0) $(LIBC) $(BUILD_DIR)/interp.o $(HEADERS)
	@echo "CC+LD-DYN $< -> $@"
	@$(CC) $(CFLAGS) -c $< -o $(BUILD_DIR)/$*.o
	@$(LD) $(LDFLAGS_DYN) -o $@ $(CRT0) $(BUILD_DIR)/interp.o $(BUILD_DIR)/$*.o $(LIBC)

# hotreload_test with embedded rtld for dlopen
RTLD_SRC := ldso/rtld.c
$(BUILD_DIR)/hotreload_test: bin/hotreload_test.c $(RTLD_SRC) $(CRT0) $(LIBC) $(HEADERS)
	@echo "CC+LD-RTLD $< -> $@"
	@$(CC) $(CFLAGS) -c $< -o $(BUILD_DIR)/hotreload_test.o
	@$(CC) $(CFLAGS) -DRTLD_EMBEDDED -c $(RTLD_SRC) -o $(BUILD_DIR)/rtld.o
	@$(LD) $(LDFLAGS) -o $@ $(CRT0) $(BUILD_DIR)/hotreload_test.o $(BUILD_DIR)/rtld.o $(LIBC)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all dirs clean
