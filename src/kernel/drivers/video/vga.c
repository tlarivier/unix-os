/*
 * vga.c — 80x25 text-mode CGA driver at 0xB8000: init, putchar with scroll
 * and hardware cursor, plus vga_print_at reserved for the panic path.
 *
 * Invariants:
 *  - VGA framebuffer access through VGA_MEM is volatile (MMIO).
 *  - CRTC programming uses VGA_CRTC_INDEX/VGA_CRTC_DATA via outb from
 * <kernel/io.h>.
 *  - cursor_x/cursor_y stay within [0, VGA_WIDTH) and [0, VGA_HEIGHT) at every
 *    observable moment; scroll_up clamps cursor_y to VGA_HEIGHT-1.
 *  - vga_init is called once at boot before any vga_putchar/vga_print_at.
 *
 * Not allowed:
 *  - Calling vfs_*, scheduler_*, or process_* from this TU.
 *  - Defining local outb/inb inlines; use <kernel/io.h>.
 *  - Exposing cursor_x/cursor_y or set_cursor_pos outside the TU.
 */

#include <kernel/io.h>
#include <kernel/ports.h>
#include <kernel/vga.h>
#include <stdint.h>

#define VGA_DEFAULT_ATTR 0x07

#define VGA_CRTC_CURSOR_HIGH 0x0E
#define VGA_CRTC_CURSOR_LOW 0x0F

static int cursor_x = 0;
static int cursor_y = 0;

#define VGA_MEM ((volatile uint16_t *)VGA_MEMORY)

static void set_cursor_pos(int x, int y) {
  uint16_t pos = y * VGA_WIDTH + x;
  outb(VGA_CRTC_INDEX, VGA_CRTC_CURSOR_HIGH);
  outb(VGA_CRTC_DATA, (pos >> 8) & 0xFF);
  outb(VGA_CRTC_INDEX, VGA_CRTC_CURSOR_LOW);
  outb(VGA_CRTC_DATA, pos & 0xFF);

  cursor_x = x;
  cursor_y = y;
}

static void scroll_up(void) {
  for (int y = 1; y < VGA_HEIGHT; y++) {
    for (int x = 0; x < VGA_WIDTH; x++) {
      VGA_MEM[(y - 1) * VGA_WIDTH + x] = VGA_MEM[y * VGA_WIDTH + x];
    }
  }
  for (int x = 0; x < VGA_WIDTH; x++) {
    VGA_MEM[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = (VGA_DEFAULT_ATTR << 8) | ' ';
  }
}

void vga_init(void) {
  for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
    VGA_MEM[i] = 0x0F20;
  }
  cursor_x = 0;
  cursor_y = 0;
  set_cursor_pos(0, 0);
}

void vga_putchar(char c) {
  if (c == '\n') {
    cursor_x = 0;
    cursor_y++;
  } else if (c == '\r') {
    cursor_x = 0;
  } else if (c == '\b') {
    if (cursor_x > 0) {
      cursor_x--;
      VGA_MEM[cursor_y * VGA_WIDTH + cursor_x] = (VGA_DEFAULT_ATTR << 8) | ' ';
    }
  } else {
    VGA_MEM[cursor_y * VGA_WIDTH + cursor_x] =
        (uint16_t)c | (VGA_DEFAULT_ATTR << 8);
    cursor_x++;
  }
  if (cursor_x >= VGA_WIDTH) {
    cursor_x = 0;
    cursor_y++;
  }
  if (cursor_y >= VGA_HEIGHT) {
    scroll_up();
    cursor_y = VGA_HEIGHT - 1;
  }
  set_cursor_pos(cursor_x, cursor_y);
}

void vga_print_at(const char *str, int x, int y, uint8_t attr) {
  int i = 0;
  while (str[i] && (x + i) < VGA_WIDTH && y < VGA_HEIGHT) {
    VGA_MEM[y * VGA_WIDTH + (x + i)] = (uint16_t)str[i] | ((uint16_t)attr << 8);
    i++;
  }
}
