#ifndef KERNEL_VGA_H
#define KERNEL_VGA_H

#include <stddef.h>
#include <stdint.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

void vga_init(void);
void vga_putchar(char c);
void vga_print_at(const char *str, int x, int y, uint8_t attr);

#endif
