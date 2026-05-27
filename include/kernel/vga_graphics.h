#ifndef KERNEL_VGA_GRAPHICS_H
#define KERNEL_VGA_GRAPHICS_H

#include <stdint.h>

#define VGA_GFX_WIDTH 320
#define VGA_GFX_HEIGHT 200
#define VGA_GFX_BPP 8
#define VGA_GFX_SIZE (VGA_GFX_WIDTH * VGA_GFX_HEIGHT)

void vga_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

int fb_open(void);
int fb_close(void);

#endif
