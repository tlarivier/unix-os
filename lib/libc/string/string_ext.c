#include <stddef.h>
#include <stdint.h>

extern void *malloc(size_t);

size_t strcspn(const char *s, const char *reject) {
    size_t count = 0;
    while (*s) {
        const char *r = reject;
        while (*r) { if (*s == *r) return count; r++; }
        s++; count++;
    }
    return count;
}

static char *strtok_pos = NULL;

char *strtok(char *str, const char *delim) {
    if (str) strtok_pos = str;
    if (!strtok_pos) return NULL;
    
    /* Skip leading delimiters */
    while (*strtok_pos) {
        const char *d = delim;
        int is_delim = 0;
        while (*d) { if (*strtok_pos == *d) { is_delim = 1; break; } d++; }
        if (!is_delim) break;
        strtok_pos++;
    }
    
    if (!*strtok_pos) return NULL;
    
    char *token = strtok_pos;
    
    /* Find end of token */
    while (*strtok_pos) {
        const char *d = delim;
        while (*d) {
            if (*strtok_pos == *d) {
                *strtok_pos++ = '\0';
                return token;
            }
            d++;
        }
        strtok_pos++;
    }
    
    return token;
}

static const char *error_strings[] = {
    "Success", "Operation not permitted", "No such file or directory",
    "No such process", "Interrupted", "I/O error", "No such device",
    "Argument list too long", "Exec format error", "Bad file descriptor",
    "No child processes", "Resource temporarily unavailable", "Out of memory",
    "Permission denied", "Bad address", "Block device required", "Device busy",
    "File exists", "Cross-device link", "No such device", "Not a directory",
    "Is a directory", "Invalid argument", "Too many open files in system",
    "Too many open files", "Not a typewriter", "Text file busy", "File too large",
    "No space left on device", "Illegal seek", "Read-only file system",
    "Too many links", "Broken pipe", "Domain error", "Range error",
    "Resource deadlock avoided", "File name too long", "No locks available",
    "Function not implemented", "Directory not empty"
};

int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        int c1 = (*s1 >= 'A' && *s1 <= 'Z') ? *s1 + 32 : *s1;
        int c2 = (*s2 >= 'A' && *s2 <= 'Z') ? *s2 + 32 : *s2;
        if (c1 != c2) return c1 - c2;
        s1++; s2++;
    }
    return *s1 - *s2;
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    while (n-- && *s1 && *s2) {
        int c1 = (*s1 >= 'A' && *s1 <= 'Z') ? *s1 + 32 : *s1;
        int c2 = (*s2 >= 'A' && *s2 <= 'Z') ? *s2 + 32 : *s2;
        if (c1 != c2) return c1 - c2;
        s1++; s2++;
    }
    return (n == (size_t)-1) ? 0 : *s1 - *s2;
}

char *strerror(int errnum) {
    if (errnum < 0 || errnum >= (int)(sizeof(error_strings)/sizeof(error_strings[0])))
        return (char*)"Unknown error";
    return (char*)error_strings[errnum];
}
