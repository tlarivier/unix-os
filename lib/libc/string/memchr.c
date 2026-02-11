#include <stddef.h>

void* memchr(const void* ptr, int value, size_t num) {
    const unsigned char* p = (const unsigned char*)ptr;
    while (num--) {
        if (*p == (unsigned char)value) return (void*)p;
        p++;
    }
    return NULL;
}
