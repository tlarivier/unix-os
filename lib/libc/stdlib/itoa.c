int itoa(int value, char* str, int base) {
    char* p = str;
    char* p1, *p2;
    int digits = 0;
    unsigned int v;
    
    if (value < 0 && base == 10) {
        *p++ = '-';
        v = -value;
    } else {
        v = (unsigned int)value;
    }
    
    do {
        int remainder = v % base;
        *p++ = (remainder < 10) ? '0' + remainder : 'a' + remainder - 10;
        digits++;
    } while (v /= base);
    
    *p = '\0';
    
    /* Reverse the digits */
    p1 = str;
    if (*p1 == '-') p1++;
    p2 = p - 1;
    while (p1 < p2) {
        char tmp = *p1;
        *p1++ = *p2;
        *p2-- = tmp;
    }
    
    return digits;
}
