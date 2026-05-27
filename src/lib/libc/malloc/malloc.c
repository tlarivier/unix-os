/*
 * malloc.c — userspace heap allocator (first-fit free-list over sbrk).
 * Exposes malloc/free/calloc/realloc plus a private sbrk helper.
 *
 * Invariants:
 *  - Heap grows monotonically via brk syscall; never shrinks.
 *  - Every block carries a header with size + 0xDEADBEEF magic for header
 *    corruption detection.
 *  - Single-threaded: no locking; one global free-list.
 *
 * Not allowed:
 *  - Coalescing adjacent free blocks (none implemented — fragmentation risk).
 *  - Detecting double-free beyond magic corruption.
 *  - Use from multi-threaded contexts (no synchronization).
 */

#include <stddef.h>
#include <stdint.h>

/* Forward declarations */
void free(void *ptr);
extern long _syscall(long num, long a1, long a2, long a3, long a4, long a5);
#define __NR_brk 40

static uint8_t *heap_start = NULL;
static uint8_t *heap_current = NULL;

typedef struct block_header {
  uint32_t size;
  uint32_t magic;
  struct block_header *next;
  int free;
} block_header_t;

#define BLOCK_MAGIC 0xDEADBEEF
#define ALIGN(x) (((x) + 7) & ~7)
#define HEADER_SIZE ALIGN(sizeof(block_header_t))

static block_header_t *free_list = NULL;

static void *sbrk(intptr_t increment) {
  if (!heap_start) {
    heap_start = (uint8_t *)_syscall(__NR_brk, 0, 0, 0, 0, 0);
    heap_current = heap_start;
  }
  if (increment == 0)
    return heap_current;

  uint8_t *old = heap_current;
  uint8_t *new_brk = heap_current + increment;

  if (_syscall(__NR_brk, (long)new_brk, 0, 0, 0, 0) != (long)new_brk) {
    return (void *)-1;
  }
  heap_current = new_brk;
  return old;
}

void *malloc(size_t size) {
  if (size == 0)
    return NULL;

  size = ALIGN(size);
  size_t total = size + HEADER_SIZE;

  block_header_t *prev = NULL;
  block_header_t *curr = free_list;

  while (curr) {
    if (curr->free && curr->size >= size) {
      curr->free = 0;
      return (void *)((uint8_t *)curr + HEADER_SIZE);
    }
    prev = curr;
    curr = curr->next;
  }

  void *block = sbrk(total);
  if (block == (void *)-1)
    return NULL;

  block_header_t *header = (block_header_t *)block;
  header->size = size;
  header->magic = BLOCK_MAGIC;
  header->next = NULL;
  header->free = 0;

  if (prev)
    prev->next = header;
  else
    free_list = header;

  return (void *)((uint8_t *)block + HEADER_SIZE);
}

void *calloc(size_t nmemb, size_t size) {
  size_t total = nmemb * size;
  void *ptr = malloc(total);
  if (ptr) {
    uint8_t *p = (uint8_t *)ptr;
    for (size_t i = 0; i < total; i++)
      p[i] = 0;
  }
  return ptr;
}

void *realloc(void *ptr, size_t size) {
  if (!ptr)
    return malloc(size);
  if (size == 0) {
    free(ptr);
    return NULL;
  }

  block_header_t *header = (block_header_t *)((uint8_t *)ptr - HEADER_SIZE);
  if (header->magic != BLOCK_MAGIC)
    return NULL;

  if (header->size >= size)
    return ptr;

  void *new_ptr = malloc(size);
  if (new_ptr) {
    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)new_ptr;
    for (size_t i = 0; i < header->size; i++)
      dst[i] = src[i];
    free(ptr);
  }
  return new_ptr;
}

void free(void *ptr) {
  if (!ptr)
    return;
  block_header_t *header = (block_header_t *)((uint8_t *)ptr - HEADER_SIZE);
  if (header->magic != BLOCK_MAGIC)
    return;
  header->free = 1;
}
