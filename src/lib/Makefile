CC := /opt/homebrew/bin/i686-elf-gcc
AR := /opt/homebrew/bin/i686-elf-ar

CFLAGS := -m32 -O2 -Wall -Wextra -nostdlib \
          -fno-pic -fno-stack-protector -fno-builtin \
          -Iinclude -I../../include -I../../uapi

BUILD_DIR := ../../build/libs
OBJ_DIR := ../../build/obj/lib

LIBC_STRING_SRCS := $(wildcard libc/string/*.c)
LIBC_STDIO_SRCS := $(wildcard libc/stdio/*.c)
LIBC_STDLIB_SRCS := $(wildcard libc/stdlib/*.c)
LIBC_CTYPE_SRCS := $(wildcard libc/ctype/*.c)
LIBC_UNISTD_SRCS := $(wildcard libc/unistd/*.c)
LIBC_MALLOC_SRCS := $(wildcard libc/malloc/*.c)
LIBC_TIME_SRCS := $(wildcard libc/time/*.c)
LIBC_MATH_SRCS := $(wildcard libc/math/*.c)
LIBC_SYSCALL_SRCS := $(wildcard libc/syscall/*.c)
LIBC_ERRNO_SRCS := $(wildcard libc/errno/*.c)

LIBC_SRCS := $(LIBC_STRING_SRCS) $(LIBC_STDIO_SRCS) $(LIBC_STDLIB_SRCS) \
             $(LIBC_CTYPE_SRCS) $(LIBC_UNISTD_SRCS) \
             $(LIBC_MALLOC_SRCS) $(LIBC_TIME_SRCS) $(LIBC_MATH_SRCS) \
             $(LIBC_SYSCALL_SRCS) $(LIBC_ERRNO_SRCS)

CRT_ASM := crt/crt0.S

LIBC_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(LIBC_SRCS))

LIBC_SO_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.pic.o,$(LIBC_SRCS))

all: dirs $(BUILD_DIR)/libc.a $(BUILD_DIR)/crt0.o $(BUILD_DIR)/libc.so

dirs:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(OBJ_DIR)/libc/string $(OBJ_DIR)/libc/stdio $(OBJ_DIR)/libc/stdlib
	@mkdir -p $(OBJ_DIR)/libc/ctype $(OBJ_DIR)/libc/unistd
	@mkdir -p $(OBJ_DIR)/libc/malloc $(OBJ_DIR)/libc/time $(OBJ_DIR)/libc/math
	@mkdir -p $(OBJ_DIR)/libc/syscall $(OBJ_DIR)/libc/errno
	@mkdir -p $(OBJ_DIR)/crt

debug:
	@echo "LIBC_SRCS: $(LIBC_SRCS)"
	@echo "LIBC_OBJS: $(LIBC_OBJS)"

$(BUILD_DIR)/libc.a: $(LIBC_OBJS)
	@echo "AR       $@"
	@$(AR) rcs $@ $^

$(BUILD_DIR)/crt0.o: $(CRT_ASM)
	@echo "AS       $<"
	@$(CC) $(CFLAGS) -c $< -o $@

LD := /opt/homebrew/bin/i686-elf-ld

$(OBJ_DIR)/%.o: %.c
	@echo "CC       $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.pic.o: %.c
	@echo "CC-PIC   $<"
	@$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(BUILD_DIR)/libc.so: $(LIBC_SO_OBJS)
	@echo "LD-SO    $@"
	@$(LD) -m elf_i386 -shared -T shared.ld -o $@ $^

clean:
	rm -rf $(OBJ_DIR)/libc $(OBJ_DIR)/userspace $(OBJ_DIR)/crt
	rm -f $(BUILD_DIR)/libc.a $(BUILD_DIR)/libc.so $(BUILD_DIR)/crt0.o

.PHONY: all dirs clean
