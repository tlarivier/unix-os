#ifndef _LIBC_FB_H
#define _LIBC_FB_H

#include <stdint.h>
#include <stddef.h>

#define FB_WIDTH  320
#define FB_HEIGHT 200
#define FB_BPP    8
#define FB_SIZE   (FB_WIDTH * FB_HEIGHT)

#define FBIOGET_VSCREENINFO  0x4600
#define FBIOPUT_VSCREENINFO  0x4601
#define FBIOGET_FSCREENINFO  0x4602
#define FBIO_WAITFORVSYNC    0x4607

struct fb_var_screeninfo {
    uint32_t xres;
    uint32_t yres;
    uint32_t bits_per_pixel;
    uint32_t xoffset;
    uint32_t yoffset;
};

struct fb_fix_screeninfo {
    char id[16];
    uint32_t smem_start;
    uint32_t smem_len;
    uint32_t line_length;
};

int fb_open(void);
void fb_close(int fd);
void *fb_mmap(int fd);
int fb_set_palette(int fd, uint8_t *palette, int count);
int fb_vsync(int fd);
void fb_blit(void *fb, const void *buffer, size_t size);

#endif 
