#include <stddef.h>
#include <stdint.h>

extern long _syscall(long num, long a1, long a2, long a3, long a4, long a5);
#define __NR_open   12
#define __NR_close  13
#define __NR_ioctl  54
#define __NR_mmap   41

#define FB_WIDTH  320
#define FB_HEIGHT 200
#define FB_SIZE   (FB_WIDTH * FB_HEIGHT)

#define FBIO_SETPALETTE  0x4610
#define FBIO_BLIT        0x4611

int fb_open(void) {
    return _syscall(__NR_open, (long)"/dev/fb0", 2, 0, 0, 0);
}

void fb_close(int fd) {
    _syscall(__NR_close, fd, 0, 0, 0, 0);
}

void *fb_mmap(int fd) {
    return (void*)_syscall(__NR_mmap, 0, FB_SIZE, 3, 1, fd);
}

int fb_set_palette(int fd, uint8_t *palette, int count) {
    struct { uint8_t *data; int count; } args = { palette, count };
    return _syscall(__NR_ioctl, fd, FBIO_SETPALETTE, (long)&args, 0, 0);
}

void fb_blit(void *fb, const void *buffer, size_t size) {
    uint8_t *dst = (uint8_t*)fb;
    const uint8_t *src = (const uint8_t*)buffer;
    for (size_t i = 0; i < size; i++) dst[i] = src[i];
}

void I_FinishUpdate(uint8_t *screens) {
    int fd = fb_open();
    if (fd >= 0) {
        _syscall(__NR_ioctl, fd, FBIO_BLIT, (long)screens, 0, 0);
        fb_close(fd);
    }
}
