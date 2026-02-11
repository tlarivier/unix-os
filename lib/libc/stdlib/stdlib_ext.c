#include <stddef.h>
#include <stdint.h>

extern long _syscall(long num, long a1, long a2, long a3, long a4, long a5);
#define __NR_exit 1

void abort(void) {
    _syscall(__NR_exit, 134, 0, 0, 0, 0);
    __builtin_unreachable();
}

void exit(int status) {
    _syscall(__NR_exit, status, 0, 0, 0, 0);
    __builtin_unreachable();
}

void _exit(int status) {
    _syscall(__NR_exit, status, 0, 0, 0, 0);
    __builtin_unreachable();
}

char *getenv(const char *name) {
    (void)name;
    return NULL;
}

int setenv(const char *name, const char *value, int overwrite) {
    (void)name; (void)value; (void)overwrite;
    return -1;
}

int unsetenv(const char *name) {
    (void)name;
    return -1;
}

int system(const char *command) {
    (void)command;
    return -1;
}

double atof(const char *s) {
    double val = 0, frac = 0;
    int neg = 0, div = 1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') { frac = frac * 10 + (*s - '0'); div *= 10; s++; }
    }
    return neg ? -(val + frac / div) : (val + frac / div);
}

unsigned long strtoul(const char *s, char **endptr, int base) {
    unsigned long val = 0;
    while (*s == ' ' || *s == '\t') s++;
    
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; s++; }
        else base = 10;
    }
    
    while (*s) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        val = val * base + d;
        s++;
    }
    if (endptr) *endptr = (char*)s;
    return val;
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void*, const void*)) {
    const char *arr = (const char*)base;
    size_t lo = 0, hi = nmemb;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const void *elem = arr + mid * size;
        int cmp = compar(key, elem);
        if (cmp == 0) return (void*)elem;
        if (cmp < 0) hi = mid;
        else lo = mid + 1;
    }
    return NULL;
}
