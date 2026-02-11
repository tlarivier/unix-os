#ifndef KERNEL_VGA_GRAPHICS_H
#define KERNEL_VGA_GRAPHICS_H

#include <stdint.h>
#include <stdbool.h>

#define VGA_GFX_WIDTH   320
#define VGA_GFX_HEIGHT  200
#define VGA_GFX_BPP     8
#define VGA_GFX_SIZE    (VGA_GFX_WIDTH * VGA_GFX_HEIGHT)

#define VGA_COLOR_BLACK         0
#define VGA_COLOR_BLUE          1
#define VGA_COLOR_GREEN         2
#define VGA_COLOR_CYAN          3
#define VGA_COLOR_RED           4
#define VGA_COLOR_MAGENTA       5
#define VGA_COLOR_BROWN         6
#define VGA_COLOR_LIGHT_GRAY    7
#define VGA_COLOR_DARK_GRAY     8
#define VGA_COLOR_LIGHT_BLUE    9
#define VGA_COLOR_LIGHT_GREEN   10
#define VGA_COLOR_LIGHT_CYAN    11
#define VGA_COLOR_LIGHT_RED     12
#define VGA_COLOR_LIGHT_MAGENTA 13
#define VGA_COLOR_YELLOW        14
#define VGA_COLOR_WHITE         15

int vga_set_mode_13h(void);
int vga_set_text_mode(void);
bool vga_is_graphics_mode(void);

void vga_putpixel(int x, int y, uint8_t color);
uint8_t vga_getpixel(int x, int y);
void vga_gfx_clear(uint8_t color);

void vga_hline(int x, int y, int width, uint8_t color);
void vga_vline(int x, int y, int height, uint8_t color);

void vga_rect(int x, int y, int w, int h, uint8_t color);
void vga_fill_rect(int x, int y, int w, int h, uint8_t color);

void vga_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void vga_get_palette(uint8_t index, uint8_t *r, uint8_t *g, uint8_t *b);
void vga_set_palette_block(const uint8_t *palette, int start, int count);
void vga_set_default_palette(void);

int vga_enable_double_buffer(uint8_t *buffer);
void vga_disable_double_buffer(void);
void vga_flip(void);

void vga_wait_vsync(void);

uint8_t *vga_get_framebuffer(void);
void vga_copy_buffer(const uint8_t *src, uint32_t size);

int vga_get_width(void);
int vga_get_height(void);

#endif
