#include <sys/types.h>
#include <unistd.h>

int main(void) {
    char c;
    
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(10), "b"(0), "c"(&c), "d"(1)
        : "memory"
    );
    
    write(1, "Got:", 4);
    write(1, &c, 1);
    write(1, "\n", 1);
    
    return 0;
}
