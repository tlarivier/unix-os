typedef long long int64_t;
typedef unsigned long long uint64_t;

int64_t __divdi3(int64_t a, int64_t b) {
    int neg = 0;
    if (a < 0) { a = -a; neg = !neg; }
    if (b < 0) { b = -b; neg = !neg; }
    
    uint64_t ua = (uint64_t)a;
    uint64_t ub = (uint64_t)b;
    uint64_t q = 0, r = 0;
    
    for (int i = 63; i >= 0; i--) {
        r <<= 1;
        r |= (ua >> i) & 1;
        if (r >= ub) { r -= ub; q |= (1ULL << i); }
    }
    
    return neg ? -(int64_t)q : (int64_t)q;
}

uint64_t __udivdi3(uint64_t a, uint64_t b) {
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r <<= 1;
        r |= (a >> i) & 1;
        if (r >= b) { r -= b; q |= (1ULL << i); }
    }
    return q;
}

int64_t __moddi3(int64_t a, int64_t b) {
    return a - __divdi3(a, b) * b;
}

uint64_t __umoddi3(uint64_t a, uint64_t b) {
    return a - __udivdi3(a, b) * b;
}
