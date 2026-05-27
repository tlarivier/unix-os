#ifndef _KERNEL_RINGBUF_H
#define _KERNEL_RINGBUF_H

#include <stddef.h>
#include <stdint.h>

typedef struct ringbuf {
  volatile uint32_t head;
  volatile uint32_t tail;
  uint32_t size;
  uint32_t mask;
  uint8_t data[];
} ringbuf_t;

#define RINGBUF_SIZE(capacity) (sizeof(ringbuf_t) + (capacity))
#define ringbuf_barrier() __sync_synchronize()

static inline void ringbuf_init(ringbuf_t *rb, uint32_t size) {
  rb->head = 0;
  rb->tail = 0;
  rb->size = size;
  rb->mask = size - 1;
}

static inline int ringbuf_empty(const ringbuf_t *rb) {
  return rb->head == rb->tail;
}

static inline int ringbuf_full(const ringbuf_t *rb) {
  return ((rb->head + 1) & rb->mask) == rb->tail;
}

static inline uint32_t ringbuf_readable(const ringbuf_t *rb) {
  return (rb->head - rb->tail) & rb->mask;
}

static inline uint32_t ringbuf_writable(const ringbuf_t *rb) {
  return (rb->tail - rb->head - 1) & rb->mask;
}

static inline int ringbuf_push(ringbuf_t *rb, uint8_t c) {
  uint32_t head = rb->head;
  uint32_t next = (head + 1) & rb->mask;

  if (next == rb->tail)
    return -1; /* Full */

  rb->data[head] = c;
  ringbuf_barrier();
  rb->head = next;
  return 0;
}

static inline int ringbuf_pop(ringbuf_t *rb, uint8_t *c) {
  uint32_t tail = rb->tail;

  if (tail == rb->head)
    return -1; /* Empty */

  *c = rb->data[tail];
  ringbuf_barrier();
  rb->tail = (tail + 1) & rb->mask;
  return 0;
}

static inline uint32_t ringbuf_write(ringbuf_t *rb, const void *buf,
                                     uint32_t len) {
  const uint8_t *src = (const uint8_t *)buf;
  uint32_t written = 0;

  while (written < len) {
    if (ringbuf_push(rb, src[written]) < 0)
      break;
    written++;
  }

  return written;
}

static inline uint32_t ringbuf_read(ringbuf_t *rb, void *buf, uint32_t len) {
  uint8_t *dst = (uint8_t *)buf;
  uint32_t read_count = 0;

  while (read_count < len) {
    if (ringbuf_pop(rb, &dst[read_count]) < 0)
      break;
    read_count++;
  }

  return read_count;
}

static inline int ringbuf_peek(const ringbuf_t *rb, uint8_t *c) {
  if (rb->tail == rb->head)
    return -1;
  *c = rb->data[rb->tail];
  return 0;
}

static inline uint32_t ringbuf_discard(ringbuf_t *rb, uint32_t n) {
  uint32_t avail = ringbuf_readable(rb);
  if (n > avail)
    n = avail;
  rb->tail = (rb->tail + n) & rb->mask;
  return n;
}

static inline void ringbuf_clear(ringbuf_t *rb) { rb->tail = rb->head; }

#endif
