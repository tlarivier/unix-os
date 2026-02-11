# Makefile for shared libraries (.so)

CC := /opt/homebrew/Cellar/i686-elf-gcc/15.2.0/bin/i686-elf-gcc
LD := /opt/homebrew/Cellar/i686-elf-binutils/2.45.1/bin/i686-elf-ld

# PIC flags for shared libraries
CFLAGS := -m32 -O2 -Wall -fPIC -fno-builtin -nostdlib -I../../include

BUILD_DIR := ../../build/libs

# Example library
EXAMPLE_SRC := example/libexample.c
EXAMPLE_OBJ := $(BUILD_DIR)/libexample.o
EXAMPLE_SO := $(BUILD_DIR)/libexample.so

all: $(EXAMPLE_SO)

$(BUILD_DIR)/%.o: example/%.c
	@mkdir -p $(BUILD_DIR)
	@echo "CC (PIC) $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(EXAMPLE_SO): $(EXAMPLE_OBJ)
	@echo "LD (SO)  $@"
	@$(LD) -shared -m elf_i386 -T ../shared.ld -o $@ $^

clean:
	rm -f $(BUILD_DIR)/libexample.o $(BUILD_DIR)/libexample.so

.PHONY: all clean
