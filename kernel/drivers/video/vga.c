#include <stdint.h>
#include <stdbool.h>
#include <kernel/drivers.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000
#define VGA_DEFAULT_ATTR 0x07

typedef struct vga_state {
    int cursor_x;
    int cursor_y;
    uint8_t current_attr;
    bool initialized;
} vga_state_t;

static vga_state_t vga_driver = {
    .cursor_x     = 0,
    .cursor_y     = 0,
    .current_attr = VGA_DEFAULT_ATTR,
    .initialized  = false
};

#define VGA_MEM ((volatile uint16_t*)VGA_MEMORY)

void vga_init(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_MEM[i] = 0x0F20;
    }
    vga_driver.cursor_x = 0;
    vga_driver.cursor_y = 0;
    vga_driver.initialized = true;
    set_cursor_pos(0, 0);
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

void set_cursor_pos(int x, int y) {
    uint16_t pos = y * VGA_WIDTH + x;
    outb(0x3D4, 14);
    outb(0x3D5, (pos >> 8) & 0xFF);
    outb(0x3D4, 15);
    outb(0x3D5, pos & 0xFF);
    
    vga_driver.cursor_x = x;
    vga_driver.cursor_y = y;
}

void get_cursor_pos(int* x, int* y) {
    if (x) *x = vga_driver.cursor_x;
    if (y) *y = vga_driver.cursor_y;
}

void clear_screen(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_MEM[i] = (VGA_DEFAULT_ATTR << 8) | ' ';
    }
    set_cursor_pos(0, 0);
}

void vga_clear(void) {
    clear_screen();
}

void scroll_up(void) {
    for (int y = 1; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            VGA_MEM[(y-1) * VGA_WIDTH + x] = VGA_MEM[y * VGA_WIDTH + x];
        }
    }
    for (int x = 0; x < VGA_WIDTH; x++) {
        VGA_MEM[(VGA_HEIGHT-1) * VGA_WIDTH + x] = (VGA_DEFAULT_ATTR << 8) | ' ';
    }
}

void vga_putchar_at(char c, int x, int y, uint8_t attr) {
    if (x >= 0 && x < VGA_WIDTH && y >= 0 && y < VGA_HEIGHT) {
        VGA_MEM[y * VGA_WIDTH + x] = (uint16_t)c | ((uint16_t)attr << 8);
    }
}

void vga_putchar(char c) {
    if (c == '\n') {
        vga_driver.cursor_x = 0;
        vga_driver.cursor_y++;
    } else if (c == '\r') {
        vga_driver.cursor_x = 0;
    } else if (c == '\b') {
        if (vga_driver.cursor_x > 0) {
            vga_driver.cursor_x--;
            VGA_MEM[vga_driver.cursor_y * VGA_WIDTH + vga_driver.cursor_x] = (VGA_DEFAULT_ATTR << 8) | ' ';
        }
    } else {
        VGA_MEM[vga_driver.cursor_y * VGA_WIDTH + vga_driver.cursor_x] = (uint16_t)c | (VGA_DEFAULT_ATTR << 8);
        vga_driver.cursor_x++;
    }
    if (vga_driver.cursor_x >= VGA_WIDTH) {
        vga_driver.cursor_x = 0;
        vga_driver.cursor_y++;
    }
    if (vga_driver.cursor_y >= VGA_HEIGHT) {
        scroll_up();
        vga_driver.cursor_y = VGA_HEIGHT - 1;
    }
    set_cursor_pos(vga_driver.cursor_x, vga_driver.cursor_y);
}

void vga_print_at(const char* str, int x, int y, uint8_t attr) {
    int i = 0;
    while (str[i] && (x + i) < VGA_WIDTH && y < VGA_HEIGHT) {
        VGA_MEM[y * VGA_WIDTH + (x + i)] = (uint16_t)str[i] | ((uint16_t)attr << 8);
        i++;
    }
}

void vga_print(const char* str) {
    while (*str) {
        vga_putchar(*str);
        str++;
    }
}

void print_hex_at(uint32_t value, int x, int y, uint8_t attr) {
    char hex[] = "0123456789ABCDEF";
    char buffer[9];
    
    for (int i = 7; i >= 0; i--) {
        buffer[7-i] = hex[(value >> (i*4)) & 0xF];
    }
    buffer[8] = '\0';
    
    vga_print_at(buffer, x, y, attr);
}

void print_decimal_at(uint32_t value, int x, int y, uint8_t attr) {
    char buffer[11];
    int i = 0;
    
    if (value == 0) {
        buffer[i++] = '0';
    } else {
        uint32_t temp = value;
        while (temp > 0) {
            buffer[i++] = '0' + (temp % 10);
            temp /= 10;
        }
    }
    for (int j = 0; j < i/2; j++) {
        char temp = buffer[j];
        buffer[j] = buffer[i-1-j];
        buffer[i-1-j] = temp;
    }
    buffer[i] = '\0';
    
    vga_print_at(buffer, x, y, attr);
}
