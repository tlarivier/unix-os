CC := /opt/homebrew/bin/i686-elf-gcc
LD := /opt/homebrew/bin/i686-elf-ld

CFLAGS := -m32 -O2 -Wall -nostdlib -fno-builtin -fno-pic -fno-stack-protector -ffreestanding \
          -I../lib/include -I../../include -I../../uapi
LDFLAGS := -m elf_i386 -T linker.ld

BUILD_DIR := ../../build/userspace
LIBC := ../../build/libs/libc.a
CRT0 := ../../build/libs/crt0.o

STATIC_BINS := init sh test_runner

ALL_BINS_FROM_SRC := $(notdir $(basename $(wildcard bin/*.c)))
DYNAMIC_BINS := $(filter-out $(STATIC_BINS),$(ALL_BINS_FROM_SRC))

HEADERS := $(wildcard include/*.h) $(wildcard ../../include/*.h) $(wildcard ../../uapi/*.h) $(wildcard ../../uapi/*.def)

LIBC_SO := ../../build/libs/libc.so
LDSO := ../../build/libs/ld.so

DOOM_DIR := doom/doomgeneric
DOOM_OBJDIR := $(BUILD_DIR)/doom_obj
DOOM_BIN := $(BUILD_DIR)/doom

DOOM_EXCLUDE := \
    doomgeneric_win.c doomgeneric_sdl.c doomgeneric_soso.c \
    doomgeneric_sosox.c doomgeneric_emscripten.c \
    doomgeneric_allegro.c doomgeneric_linuxvt.c \
    doomgeneric_xlib.c \
    i_endoom.c i_joystick.c \
    i_allegromusic.c i_allegrosound.c i_sdlmusic.c i_sdlsound.c

DOOM_ALL_SRCS := $(notdir $(wildcard $(DOOM_DIR)/*.c))
DOOM_SRCS := $(filter-out $(DOOM_EXCLUDE),$(DOOM_ALL_SRCS))
DOOM_OBJS := $(addprefix $(DOOM_OBJDIR)/,$(DOOM_SRCS:.c=.o))

DOOM_CFLAGS := -m32 -O2 -nostdlib -fno-builtin -fno-pic -fno-stack-protector \
               -ffreestanding -std=gnu99 \
               -DNORMALUNIX -DUNIXOS -DCMAP256 \
               -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
               -I$(DOOM_DIR) \
               -I../lib/include -I../../include -I../../uapi \
               -Wno-implicit-function-declaration \
               -Wno-int-conversion \
               -Wno-incompatible-pointer-types \
               -Wno-pointer-sign \
               -Wno-discarded-qualifiers \
               -Wno-unused-but-set-variable \
               -Wno-unused-variable \
               -Wno-unused-function \
               -Wno-format \
               -Wno-strict-aliasing
DOOM_EXISTS := $(wildcard $(DOOM_DIR)/doomgeneric.c)

all: dirs $(addprefix $(BUILD_DIR)/,$(STATIC_BINS)) $(addprefix $(BUILD_DIR)/,$(DYNAMIC_BINS)) $(if $(DOOM_EXISTS),$(DOOM_BIN))

dirs:
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%: bin/%.c $(CRT0) $(LIBC) $(HEADERS)
	@echo "CC+LD    $< -> $@"
	@$(CC) $(CFLAGS) -c $< -o $(BUILD_DIR)/$*.o
	@$(LD) $(LDFLAGS) -o $@ $(CRT0) $(BUILD_DIR)/$*.o $(LIBC)

LDFLAGS_DYN := -m elf_i386 -T linker_dynamic.ld --dynamic-linker=/lib/ld.so
$(BUILD_DIR)/interp.o: 
	@echo "GEN      interp.o"
	@echo '.section .interp,"a",@progbits' > $(BUILD_DIR)/interp.s
	@echo '.asciz "/lib/ld.so"' >> $(BUILD_DIR)/interp.s
	@$(CC) -m32 -c $(BUILD_DIR)/interp.s -o $@

$(addprefix $(BUILD_DIR)/,$(DYNAMIC_BINS)): $(BUILD_DIR)/%: bin/%.c $(CRT0) $(LIBC) $(BUILD_DIR)/interp.o $(HEADERS)
	@echo "CC+LD-DYN $< -> $@"
	@$(CC) $(CFLAGS) -c $< -o $(BUILD_DIR)/$*.o
	@$(LD) $(LDFLAGS_DYN) -o $@ $(CRT0) $(BUILD_DIR)/interp.o $(BUILD_DIR)/$*.o $(LIBC)

$(DOOM_OBJDIR):
	@mkdir -p $(DOOM_OBJDIR)

$(DOOM_OBJDIR)/%.o: $(DOOM_DIR)/%.c | $(DOOM_OBJDIR)
	@echo "CC-DOOM   $< -> $@"
	@$(CC) $(DOOM_CFLAGS) -c $< -o $@

$(DOOM_BIN): $(CRT0) $(LIBC) $(DOOM_OBJS)
	@echo "LD-DOOM   $@"
	@$(LD) $(LDFLAGS) -o $@ $(CRT0) $(DOOM_OBJS) $(LIBC)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all dirs clean
