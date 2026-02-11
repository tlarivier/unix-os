#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <kernel/vga_graphics.h>
#include <kernel/io.h>
#include <kernel/ports.h>

#define VGA_GFX_WIDTH      320
#define VGA_GFX_HEIGHT     200
#define VGA_GFX_MEMORY     0xA0000
#define VGA_GFX_SIZE       (VGA_GFX_WIDTH * VGA_GFX_HEIGHT)

typedef struct {
    uint8_t *framebuffer;
    uint8_t *backbuffer;
    bool graphics_mode;
    bool double_buffering;
} vga_gfx_state_t;

static vga_gfx_state_t gfx = {
    .framebuffer      = (uint8_t*)VGA_GFX_MEMORY,
    .backbuffer       = NULL,
    .graphics_mode    = false,
    .double_buffering = false
};

static const uint8_t mode13h_misc = 0x63;

static const uint8_t mode13h_seq[] = {
    0x03,  /* Reset */
    0x01,  /* Clocking Mode */
    0x0F,  /* Map Mask */
    0x00,  /* Character Map Select */
    0x0E   /* Sequencer Memory Mode */
};

static const uint8_t mode13h_crtc[] = {
    0x5F,  /* Horizontal Total */
    0x4F,  /* Horizontal Display End */
    0x50,  /* Start Horizontal Blanking */
    0x82,  /* End Horizontal Blanking */
    0x54,  /* Start Horizontal Retrace */
    0x80,  /* End Horizontal Retrace */
    0xBF,  /* Vertical Total */
    0x1F,  /* Overflow */
    0x00,  /* Preset Row Scan */
    0x41,  /* Maximum Scan Line */
    0x00,  /* Cursor Start */
    0x00,  /* Cursor End */
    0x00,  /* Start Address High */
    0x00,  /* Start Address Low */
    0x00,  /* Cursor Location High */
    0x00,  /* Cursor Location Low */
    0x9C,  /* Vertical Retrace Start */
    0x0E,  /* Vertical Retrace End */
    0x8F,  /* Vertical Display End */
    0x28,  /* Offset */
    0x40,  /* Underline Location */
    0x96,  /* Start Vertical Blanking */
    0xB9,  /* End Vertical Blanking */
    0xA3,  /* CRTC Mode Control */
    0xFF   /* Line Compare */
};

static const uint8_t mode13h_gc[] = {
    0x00,  /* Set/Reset */
    0x00,  /* Enable Set/Reset */
    0x00,  /* Color Compare */
    0x00,  /* Data Rotate */
    0x00,  /* Read Map Select */
    0x40,  /* Graphics Mode */
    0x05,  /* Miscellaneous Graphics */
    0x0F,  /* Color Don't Care */
    0xFF   /* Bit Mask */
};

static const uint8_t mode13h_ac[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x41,  /* Attribute Mode Control */
    0x00,  /* Overscan Color */
    0x0F,  /* Color Plane Enable */
    0x00,  /* Horizontal Pixel Panning */
    0x00   /* Color Select */
};

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
        inb(VGA_INSTAT_READ);  /* Reset flip-flop */
        outb(VGA_AC_INDEX, i);
        outb(VGA_AC_WRITE, mode13h_ac[i]);
    }
    
    inb(VGA_INSTAT_READ);
    outb(VGA_AC_INDEX, 0x20);
}

#define VGA_DAC_WRITE_INDEX 0x3C8
#define VGA_DAC_DATA       0x3C9
#define VGA_DAC_READ_INDEX 0x3C7

void vga_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    outb(VGA_DAC_WRITE_INDEX, index);
    outb(VGA_DAC_DATA, r >> 2);
    outb(VGA_DAC_DATA, g >> 2);
    outb(VGA_DAC_DATA, b >> 2);
}

void vga_get_palette(uint8_t index, uint8_t *r, uint8_t *g, uint8_t *b) {
    outb(VGA_DAC_READ_INDEX, index);
    *r = inb(VGA_DAC_DATA) << 2;
    *g = inb(VGA_DAC_DATA) << 2;
    *b = inb(VGA_DAC_DATA) << 2;
}

void vga_set_palette_block(const uint8_t *palette, int start, int count) {
    outb(VGA_DAC_WRITE_INDEX, start);
    for (int i = 0; i < count * 3; i++) {
        outb(VGA_DAC_DATA, palette[i] >> 2);
    }
}

int vga_set_mode_13h(void) {
    vga_write_regs();
    for (uint32_t i = 0; i < VGA_GFX_SIZE; i++) {
        gfx.framebuffer[i] = 0;
    }
    gfx.graphics_mode = true;
    return 0;
}

int vga_set_text_mode(void) {
    /* Use BIOS call via real mode - simplified: just mark as text mode */
    /* In a full implementation, we'd need to restore text mode registers */
    gfx.graphics_mode = false;
    
    extern void vga_init(void);
    vga_init();
    
    return 0;
}

bool vga_is_graphics_mode(void) {
    return gfx.graphics_mode;
}

void vga_putpixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= VGA_GFX_WIDTH || y < 0 || y >= VGA_GFX_HEIGHT) {
        return;
    }
    
    uint8_t *target = gfx.double_buffering ? gfx.backbuffer : gfx.framebuffer;
    target[y * VGA_GFX_WIDTH + x] = color;
}

uint8_t vga_getpixel(int x, int y) {
    if (x < 0 || x >= VGA_GFX_WIDTH || y < 0 || y >= VGA_GFX_HEIGHT) {
        return 0;
    }
    return gfx.framebuffer[y * VGA_GFX_WIDTH + x];
}

void vga_gfx_clear(uint8_t color) {
    uint8_t *target = gfx.double_buffering ? gfx.backbuffer : gfx.framebuffer;
    for (uint32_t i = 0; i < VGA_GFX_SIZE; i++) {
        target[i] = color;
    }
}

void vga_hline(int x, int y, int width, uint8_t color) {
    if (y < 0 || y >= VGA_GFX_HEIGHT) return;
    if (x < 0) { width += x; x = 0; }
    if (x + width > VGA_GFX_WIDTH) width = VGA_GFX_WIDTH - x;
    if (width <= 0) return;
    
    uint8_t *target = gfx.double_buffering ? gfx.backbuffer : gfx.framebuffer;
    uint8_t *dst = target + y * VGA_GFX_WIDTH + x;
    
    for (int i = 0; i < width; i++) {
        dst[i] = color;
    }
}

void vga_vline(int x, int y, int height, uint8_t color) {
    if (x < 0 || x >= VGA_GFX_WIDTH) return;
    if (y < 0) { height += y; y = 0; }
    if (y + height > VGA_GFX_HEIGHT) height = VGA_GFX_HEIGHT - y;
    if (height <= 0) return;
    
    uint8_t *target = gfx.double_buffering ? gfx.backbuffer : gfx.framebuffer;
    
    for (int i = 0; i < height; i++) {
        target[(y + i) * VGA_GFX_WIDTH + x] = color;
    }
}

void vga_rect(int x, int y, int w, int h, uint8_t color) {
    vga_hline(x, y, w, color);
    vga_hline(x, y + h - 1, w, color);
    vga_vline(x, y, h, color);
    vga_vline(x + w - 1, y, h, color);
}

void vga_fill_rect(int x, int y, int w, int h, uint8_t color) {
    for (int i = 0; i < h; i++) {
        vga_hline(x, y + i, w, color);
    }
}

void vga_set_default_palette(void) {
    /* First 16 colors: standard CGA colors */
    static const uint8_t cga_palette[16][3] = {
        {0, 0, 0},       /* Black */
        {0, 0, 42},      /* Blue */
        {0, 42, 0},      /* Green */
        {0, 42, 42},     /* Cyan */
        {42, 0, 0},      /* Red */
        {42, 0, 42},     /* Magenta */
        {42, 21, 0},     /* Brown */
        {42, 42, 42},    /* Light Gray */
        {21, 21, 21},    /* Dark Gray */
        {21, 21, 63},    /* Light Blue */
        {21, 63, 21},    /* Light Green */
        {21, 63, 63},    /* Light Cyan */
        {63, 21, 21},    /* Light Red */
        {63, 21, 63},    /* Light Magenta */
        {63, 63, 21},    /* Yellow */
        {63, 63, 63}     /* White */
    };
    
    for (int i = 0; i < 16; i++) {
        vga_set_palette(i, cga_palette[i][0], cga_palette[i][1], cga_palette[i][2]);
    }
    
    /* Colors 16-231: 6x6x6 RGB cube */
    int idx = 16;
    for (int r = 0; r < 6; r++) {
        for (int g = 0; g < 6; g++) {
            for (int b = 0; b < 6; b++) {
                vga_set_palette(idx++, r * 12, g * 12, b * 12);
            }
        }
    }
    
    /* Colors 232-255: grayscale */
    for (int i = 0; i < 24; i++) {
        uint8_t gray = i * 2 + 8;
        vga_set_palette(232 + i, gray, gray, gray);
    }
}

int vga_enable_double_buffer(uint8_t *buffer) {
    if (!buffer) return -1;
    
    gfx.backbuffer = buffer;
    gfx.double_buffering = true;
    return 0;
}

void vga_disable_double_buffer(void) {
    gfx.double_buffering = false;
    gfx.backbuffer = NULL;
}

void vga_flip(void) {
    if (!gfx.double_buffering || !gfx.backbuffer) return;
    
    uint32_t *src = (uint32_t*)gfx.backbuffer;
    uint32_t *dst = (uint32_t*)gfx.framebuffer;
    
    for (uint32_t i = 0; i < VGA_GFX_SIZE / 4; i++) {
        dst[i] = src[i];
    }
}

void vga_wait_vsync(void) {
    while (inb(VGA_INSTAT_READ) & 0x08);
    while (!(inb(VGA_INSTAT_READ) & 0x08));
}

uint8_t *vga_get_framebuffer(void) {
    return gfx.framebuffer;
}

int vga_get_width(void) {
    return VGA_GFX_WIDTH;
}

int vga_get_height(void) {
    return VGA_GFX_HEIGHT;
}

void vga_copy_buffer(const uint8_t *src, uint32_t size) {
    if (size > VGA_GFX_SIZE) size = VGA_GFX_SIZE;
    
    uint8_t *target = gfx.double_buffering ? gfx.backbuffer : gfx.framebuffer;
    
    uint32_t *s = (uint32_t*)src;
    uint32_t *d = (uint32_t*)target;
    uint32_t words = size / 4;
    
    for (uint32_t i = 0; i < words; i++) {
        d[i] = s[i];
    }
}
