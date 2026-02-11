#include <kernel/constants.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/vga_graphics.h>
#include <kernel/errno.h>
#include <kernel/vfs.h>

#define FBIOGET_VSCREENINFO  0x4600
#define FBIOPUT_VSCREENINFO  0x4601
#define FBIOGET_FSCREENINFO  0x4602
#define FBIOPAN_DISPLAY      0x4606
#define FBIO_WAITFORVSYNC    0x4020

struct fb_var_screeninfo {
    uint32_t xres;           
    uint32_t yres;
    uint32_t xres_virtual;   
    uint32_t yres_virtual;
    uint32_t xoffset;        
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;      
    uint32_t red_offset, red_length;
    uint32_t green_offset, green_length;
    uint32_t blue_offset, blue_length;
    uint32_t transp_offset, transp_length;
};

struct fb_fix_screeninfo {
    char id[16];             
    uint32_t smem_start;     
    uint32_t smem_len;       
    uint32_t type;           
    uint32_t visual;         
    uint32_t line_length;    
};

static int fb_mode_active = 0;

static struct fb_var_screeninfo fb_var = {
    .xres = 320,
    .yres = 200,
    .xres_virtual = 320,
    .yres_virtual = 200,
    .xoffset = 0,
    .yoffset = 0,
    .bits_per_pixel = 8,
    .grayscale = 0,
    .red_offset = 0, .red_length = 8,
    .green_offset = 0, .green_length = 8,
    .blue_offset = 0, .blue_length = 8,
    .transp_offset = 0, .transp_length = 0
};

static struct fb_fix_screeninfo fb_fix = {
    .id = "VGA Mode 13h",
    .smem_start = VGA_FRAMEBUFFER_ADDR,
    .smem_len = 64000,       
    .type = 0,              
    .visual = 3,            
    .line_length = 320
};

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
        vga_set_text_mode();
        fb_mode_active = 0;
    }
    return 0;
}

ssize_t fb_read(void *buf, size_t count, off_t offset) {
    if (!fb_mode_active) return -ENODEV;
    if (offset >= 64000) return 0;
    if (offset + count > 64000) count = 64000 - offset;
    
    uint8_t *fb = vga_get_framebuffer();
    uint8_t *dst = (uint8_t*)buf;
    
    for (size_t i = 0; i < count; i++) {
        dst[i] = fb[offset + i];
    }
    
    return (ssize_t)count;
}

ssize_t fb_write(const void *buf, size_t count, off_t offset) {
    if (!fb_mode_active) {
        /* Auto-enable graphics mode on first write */
        fb_open();
    }
    
    if (offset >= 64000) return 0;
    if (offset + count > 64000) count = 64000 - offset;
    
    uint8_t *fb = vga_get_framebuffer();
    const uint8_t *src = (const uint8_t*)buf;
    
    for (size_t i = 0; i < count; i++) {
        fb[offset + i] = src[i];
    }
    
    return (ssize_t)count;
}

int fb_ioctl(uint32_t cmd, void *arg) {
    switch (cmd) {
        case FBIOGET_VSCREENINFO:
            if (arg) {
                struct fb_var_screeninfo *info = (struct fb_var_screeninfo*)arg;
                *info = fb_var;
            }
            return 0;
            
        case FBIOPUT_VSCREENINFO:
            return 0;
            
        case FBIOGET_FSCREENINFO:
            if (arg) {
                struct fb_fix_screeninfo *info = (struct fb_fix_screeninfo*)arg;
                *info = fb_fix;
            }
            return 0;
            
        case FBIO_WAITFORVSYNC:
            vga_wait_vsync();
            return 0;
            
        case FBIOPAN_DISPLAY:
            /* Not supported for Mode 13h */
            return 0;
            
        default:
            return -EINVAL;
    }
}

uint32_t fb_get_phys_addr(void) {
    return VGA_FRAMEBUFFER_ADDR;
}

uint32_t fb_get_size(void) {
    return 64000;
}

void fb_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    vga_set_palette(index, r >> 2, g >> 2, b >> 2);
}
