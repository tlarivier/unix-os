#include <stddef.h>

extern size_t strlen(const char* str);
extern void* malloc(size_t size);
extern void* memcpy(void* dest, const void* src, size_t num);

char* strdup(const char* str) {
    size_t len = strlen(str) + 1;
    char* dup = (char*)malloc(len);
    if (dup) memcpy(dup, str, len);
    return dup;
}
