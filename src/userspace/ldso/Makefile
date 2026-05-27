CC := /opt/homebrew/bin/i686-elf-gcc
LD := /opt/homebrew/bin/i686-elf-ld

CFLAGS := -m32 -O2 -Wall -Wextra -nostdlib -fno-builtin \
          -fno-pic -fno-stack-protector -ffreestanding

BUILD_DIR := ../../../build/libs

all: dirs $(BUILD_DIR)/ld.so

dirs:
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/ld.so: rtld.c start.S ldso.ld
	@echo "CC+LD    rtld.c + start.S -> $@"
	@$(CC) $(CFLAGS) -fPIC -c rtld.c -o rtld.o
	@$(CC) $(CFLAGS) -c start.S -o start.o
	@$(LD) -m elf_i386 -shared -Bsymbolic -T ldso.ld -o $@ start.o rtld.o
	@rm -f rtld.o start.o

clean:
	rm -f $(BUILD_DIR)/ld.so rtld.o

.PHONY: all dirs clean
