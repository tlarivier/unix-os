/*
 * vga_graphics.c — programs VGA registers for mode 13h (320x200x8) and exposes
 * fb_open/fb_close/vga_set_palette used by sys_gfx_mode/sys_gfx_palette so Doom
 * can mmap 0xA0000 directly.
 *
 * Invariants:
 *  - All VGA register writes go through outb/inb from <kernel/io.h>.
 *  - fb_mode_active reflects current chip state: true between fb_open success
 *    and fb_close; sys_gfx_mode is the only caller path that mutates it.
 *  - Mode tables (mode13h_*) are const and file-static; vga_write_regs is
 *    called only from this TU.
 *  - vga_set_palette reprograms VGA_DAC_* registers; values are 6-bit (0..63).
 *
 * Not allowed:
 *  - Calling vfs_*, scheduler_*, process_* from this TU.
 *  - Holding any lock across the outb sequence (CRTC programming is BSP-only).
 *  - Exporting mode tables, vga_write_regs, or fb_mode_active outside the TU.
 */

#include <kernel/io.h>
#include <kernel/ports.h>
#include <kernel/vga.h>
#include <kernel/vga_graphics.h>
#include <stdint.h>

#define VGA_GFX_MEMORY 0xA0000

static const uint8_t mode13h_misc = 0x63;

static const uint8_t mode13h_seq[] = {0x03, 0x01, 0x0F, 0x00, 0x0E};

static const uint8_t mode13h_crtc[] = {0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF,
                                       0x1F, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00,
                                       0x00, 0x00, 0x9C, 0x0E, 0x8F, 0x28, 0x40,
                                       0x96, 0xB9, 0xA3, 0xFF};

static const uint8_t mode13h_gc[] = {0x00, 0x00, 0x00, 0x00, 0x00,
                                     0x40, 0x05, 0x0F, 0xFF};

static const uint8_t mode13h_ac[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                                     0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
                                     0x0E, 0x0F, 0x41, 0x00, 0x0F, 0x00, 0x00};

static void vga_write_regs(void) {
  uint32_t i;

  outb(VGA_MISC_WRITE, mode13h_misc);

  for (i = 0; i < sizeof(mode13h_seq); i++) {
    outb(VGA_SEQ_INDEX, i);
    outb(VGA_SEQ_DATA, mode13h_seq[i]);
  }

  outb(VGA_CRTC_INDEX, 0x03);
  outb(VGA_CRTC_DATA, inb(VGA_CRTC_DATA) | 0x80);
  outb(VGA_CRTC_INDEX, 0x11);
  outb(VGA_CRTC_DATA, inb(VGA_CRTC_DATA) & ~0x80);

  for (i = 0; i < sizeof(mode13h_crtc); i++) {
    outb(VGA_CRTC_INDEX, i);
    outb(VGA_CRTC_DATA, mode13h_crtc[i]);
  }

  for (i = 0; i < sizeof(mode13h_gc); i++) {
    outb(VGA_GC_INDEX, i);
    outb(VGA_GC_DATA, mode13h_gc[i]);
  }

  for (i = 0; i < sizeof(mode13h_ac); i++) {
    inb(VGA_INSTAT_READ);
    outb(VGA_AC_INDEX, i);
    outb(VGA_AC_WRITE, mode13h_ac[i]);
  }

  inb(VGA_INSTAT_READ);
  outb(VGA_AC_INDEX, 0x20);
}

void vga_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
  outb(VGA_DAC_WRITE_INDEX, index);
  outb(VGA_DAC_DATA, r >> 2);
  outb(VGA_DAC_DATA, g >> 2);
  outb(VGA_DAC_DATA, b >> 2);
}

static int vga_set_mode_13h(void) {
  uint8_t *fb = (uint8_t *)VGA_GFX_MEMORY;
  vga_write_regs();
  for (uint32_t i = 0; i < VGA_GFX_SIZE; i++) {
    fb[i] = 0;
  }
  return 0;
}

static int vga_text_clear(void) {
  vga_init();
  return 0;
}

static void vga_set_default_palette(void) {
  /* First 16 colors: standard CGA colors */
  static const uint8_t cga_palette[16][3] = {
      {0, 0, 0},    /* Black */
      {0, 0, 42},   /* Blue */
      {0, 42, 0},   /* Green */
      {0, 42, 42},  /* Cyan */
      {42, 0, 0},   /* Red */
      {42, 0, 42},  /* Magenta */
      {42, 21, 0},  /* Brown */
      {42, 42, 42}, /* Light Gray */
      {21, 21, 21}, /* Dark Gray */
      {21, 21, 63}, /* Light Blue */
      {21, 63, 21}, /* Light Green */
      {21, 63, 63}, /* Light Cyan */
      {63, 21, 21}, /* Light Red */
      {63, 21, 63}, /* Light Magenta */
      {63, 63, 21}, /* Yellow */
      {63, 63, 63}  /* White */
  };

  for (int i = 0; i < 16; i++) {
    vga_set_palette(i, cga_palette[i][0], cga_palette[i][1], cga_palette[i][2]);
  }

  int idx = 16;
  for (int r = 0; r < 6; r++) {
    for (int g = 0; g < 6; g++) {
      for (int b = 0; b < 6; b++) {
        vga_set_palette(idx++, r * 12, g * 12, b * 12);
      }
    }
  }

  for (int i = 0; i < 24; i++) {
    uint8_t gray = i * 2 + 8;
    vga_set_palette(232 + i, gray, gray, gray);
  }
}

static int fb_mode_active = 0;

int fb_open(void) {
  if (!fb_mode_active) {
    vga_set_mode_13h();
    vga_set_default_palette();
    fb_mode_active = 1;
  }
  return 0;
}

int fb_close(void) {
  if (fb_mode_active) {
    vga_text_clear();
    fb_mode_active = 0;
  }
  return 0;
}
