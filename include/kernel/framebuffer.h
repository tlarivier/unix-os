#ifndef KERNEL_FRAMEBUFFER_H
#define KERNEL_FRAMEBUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/types.h>

#define FBIOGET_VSCREENINFO  0x4600
#define FBIOPUT_VSCREENINFO  0x4601
#define FBIOGET_FSCREENINFO  0x4602
#define FBIOPAN_DISPLAY      0x4606
#define FBIO_WAITFORVSYNC    0x4020

int fb_open(void);
int fb_close(void);
ssize_t fb_read(void *buf, size_t count, off_t offset);
ssize_t fb_write(const void *buf, size_t count, off_t offset);
int fb_ioctl(uint32_t cmd, void *arg);

uint32_t fb_get_phys_addr(void);
uint32_t fb_get_size(void);

void fb_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

#endif 
