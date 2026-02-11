#ifndef _KERNEL_RANDOM_H
#define _KERNEL_RANDOM_H

#include <stdint.h>

extern uint32_t __random_state;
void random_init(void);

static inline uint32_t random32(void) {
    uint32_t x = __random_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    __random_state = x;
    return x;
}

static inline uint32_t random_range(uint32_t max) {
    if (max == 0) return 0;
    return random32() % max;
}

static inline uint32_t random_between(uint32_t min, uint32_t max) {
    if (min >= max) return min;
    return min + random_range(max - min + 1);
}

static inline void get_random_bytes(void *buf, uint32_t len) {
    uint8_t *p = (uint8_t *)buf;
    while (len >= 4) {
        uint32_t r = random32();
        *p++ = r & 0xFF;
        *p++ = (r >> 8) & 0xFF;
        *p++ = (r >> 16) & 0xFF;
        *p++ = (r >> 24) & 0xFF;
        len -= 4;
    }
    if (len > 0) {
        uint32_t r = random32();
        while (len--) {
            *p++ = r & 0xFF;
            r >>= 8;
        }
    }
}

static inline uint32_t random_canary(void) {
    return (random32() & 0xFFFFFF00) | 0x00;
}

static inline uint32_t random_aslr_offset(uint32_t align, uint32_t max_pages) {
    uint32_t pages = random_range(max_pages);
    return pages * align;
}

void random_add_entropy(uint32_t val);
uint32_t random_secure(void);

#endif 
