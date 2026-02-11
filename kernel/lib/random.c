#include <kernel/random.h>
#include <stdint.h>

uint32_t __random_state = 0xDEADBEEF;

static uint32_t entropy_pool[4] = {0x12345678, 0x9ABCDEF0, 0xFEDCBA98, 0x76543210};
static uint32_t entropy_index   = 0;

void random_init(void) {
    uint32_t lo, hi;
    
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    
    __random_state = lo ^ hi ^ 0xDEADBEEF;
    
    entropy_pool[0] ^= lo;
    entropy_pool[1] ^= hi;
    entropy_pool[2] ^= lo ^ hi;
    entropy_pool[3] ^= (lo << 16) | (hi >> 16);
    
    for (int i = 0; i < 32; i++) {
        random32();
    }
}

void random_add_entropy(uint32_t val) {
    uint32_t idx = entropy_index++ & 3;
    entropy_pool[idx] ^= val;
    entropy_pool[idx] ^= entropy_pool[(idx + 1) & 3] >> 7;
    entropy_pool[(idx + 2) & 3] ^= val << 13;
    
    if ((entropy_index & 0xF) == 0) {
        __random_state ^= entropy_pool[0] ^ entropy_pool[1] ^ 
                         entropy_pool[2] ^ entropy_pool[3];
    }
}

uint32_t random_secure(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    
    random_add_entropy(lo ^ hi);
    
    uint32_t r = random32();
    r ^= entropy_pool[r & 3];
    r ^= entropy_pool[(r >> 8) & 3] << 8;
    
    return r;
}
