# Makefile for userspace modules (shared libraries)

CC := /opt/homebrew/Cellar/i686-elf-gcc/15.2.0/bin/i686-elf-gcc
LD := /opt/homebrew/Cellar/i686-elf-binutils/2.45.1/bin/i686-elf-ld

CFLAGS := -m32 -O2 -Wall -Wextra -nostdlib -fPIC \
          -fno-stack-protector -fno-builtin \
          -I../../include -I../include

BUILD_DIR := ../../build/modules

MODULES := testmod.so

all: dirs $(addprefix $(BUILD_DIR)/,$(MODULES))

dirs:
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.so: %.c
	@echo "CC-SO    $< -> $@"
	@$(CC) $(CFLAGS) -shared -c $< -o $(BUILD_DIR)/$*.o
	@$(LD) -m elf_i386 -shared -o $@ $(BUILD_DIR)/$*.o

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all dirs clean
