#include <stddef.h>

void* memmove(void* dest, const void* src, size_t num) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s) {
        while (num--) *d++ = *s++;
    } else {
        d += num; s += num;
        while (num--) *--d = *--s;
    }
    return dest;
}
