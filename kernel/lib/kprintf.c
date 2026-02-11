#include <kernel/kprintf.h>
#include <kernel/vga.h>
#include <kernel/serial.h>
#include <stdarg.h>

static void kputchar(char c) {
    vga_putchar(c);
}

static void kprint_uint(uint32_t val, uint32_t base) {
    char buf[33];
    const char* digits = "0123456789ABCDEF";
    int i = 0;
    
    if (val == 0) {
        kputchar('0');
        return;
    }
    
    while (val && i < 32) {
        buf[i++] = digits[val % base];
        val /= base;
    }
    
    while (i--) {
        kputchar(buf[i]);
    }
}

static void kprint_int(int32_t v) {
    if (v < 0) {
        kputchar('-');
        if (v == -2147483648) {
            kprint_uint(2147483648U, 10);
        } else {
            kprint_uint((uint32_t)(-v), 10);
        }
    } else {
        kprint_uint((uint32_t)v, 10);
    }
}

void kprintf(const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    
    for (const char* p = format; *p; ++p) {
        if (*p != '%') {
            kputchar(*p);
            continue;
        }
        
        ++p;
        if (*p == '\0') break;
        
        switch (*p) {
            case '%': 
                kputchar('%'); 
                break;
            case 's': {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                while (*s) kputchar(*s++);
                break;
            }
            case 'u': 
                kprint_uint(va_arg(ap, uint32_t), 10); 
                break;
            case 'x': 
                kprint_uint(va_arg(ap, uint32_t), 16); 
                break;
            case 'p': {
                uint32_t ptr = (uint32_t)va_arg(ap, void*);
                kputchar('0'); 
                kputchar('x');
                kprint_uint(ptr, 16);
                break;
            }
            case 'd': 
                kprint_int(va_arg(ap, int32_t)); 
                break;
            default:
                kputchar('%');
                kputchar(*p);
                break;
        }
    }
    
    va_end(ap);
}

void itoa(uint32_t num, char* buffer, int base) {
    if (num == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }
    
    int i = 0;
    while (num > 0) {
        int digit = num % base;
        buffer[i++] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
        num /= base;
    }
    buffer[i] = '\0';
    for (int j = 0; j < i / 2; j++) {
        char tmp = buffer[j];
        buffer[j] = buffer[i - 1 - j];
        buffer[i - 1 - j] = tmp;
    }
}
