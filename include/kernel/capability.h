#ifndef KERNEL_CAPABILITY_H
#define KERNEL_CAPABILITY_H

#include <stdint.h>
#include <stdbool.h>

#define CAP_SYS_NICE    23
#define CAP_SYS_ADMIN   21
#define CAP_SETUID      7
#define CAP_SETGID      6
#define CAP_KILL        5

#define CAP_LAST_CAP    31
#define CAP_SET_SIZE    ((CAP_LAST_CAP + 31) / 32)

typedef struct cap_set {
    uint32_t caps[CAP_SET_SIZE];
} cap_set_t;

static inline bool cap_isset(const cap_set_t *set, int cap) {
    if (cap < 0 || cap > CAP_LAST_CAP) return false;
    return (set->caps[cap / 32] & (1U << (cap % 32))) != 0;
}

static inline void cap_set(cap_set_t *set, int cap) {
    if (cap < 0 || cap > CAP_LAST_CAP) return;
    set->caps[cap / 32] |= (1U << (cap % 32));
}

static inline void cap_clear(cap_set_t *set, int cap) {
    if (cap < 0 || cap > CAP_LAST_CAP) return;
    set->caps[cap / 32] &= ~(1U << (cap % 32));
}

static inline void cap_clear_all(cap_set_t *set) {
    for (int i = 0; i < CAP_SET_SIZE; i++) {
        set->caps[i] = 0;
    }
}

bool capable(int cap);

void capability_init(void);

#endif 
