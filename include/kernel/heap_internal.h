#ifndef KERNEL_HEAP_INTERNAL_H
#define KERNEL_HEAP_INTERNAL_H

#include <stdint.h>
#include <stddef.h>

typedef struct block_header {
    uint32_t size;                  
    uint32_t is_free;               
    struct block_header* next;      
    struct block_header* prev;      
} block_header_t;

#endif 
