#ifndef KERNEL_DRIVERS_H
#define KERNEL_DRIVERS_H

#include <stdint.h>
#include <stddef.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

#define VGA_CRTC_ADDR    0x3D4
#define VGA_CRTC_DATA    0x3D5
#define VGA_CURSOR_HIGH  0x0E
#define VGA_CURSOR_LOW   0x0F

void vga_init(void);
void vga_clear(void);
void vga_putchar(char c);
void vga_print(const char* str);
void vga_print_at(const char* str, int x, int y, uint8_t color);
void set_cursor_pos(int x, int y);
void scroll_up(void);

#define KEYBOARD_PORT_DATA    0x60
#define KEYBOARD_PORT_STATUS  0x64

void keyboard_init(void);
char keyboard_getchar(void);
int keyboard_available(void);

#define TIMER_FREQUENCY 1000  // 1000 Hz = 1ms resolution
#define PIT_CHANNEL_0   0x40
#define PIT_COMMAND     0x43

void timer_init(uint32_t frequency);
uint32_t get_timer_ticks(void);
void timer_sleep(uint32_t milliseconds);

#endif 
