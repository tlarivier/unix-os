/*
 * keyboard.c — PS/2 driver (port 0x60) that decodes XT scancodes to ASCII
 * (Shift/Ctrl/CapsLock/extended state) and feeds two ringbufs: an ASCII
 * stream for the TTY/shell and a raw scancode+flags event stream for Doom.
 *
 * Invariants:
 *  - All port reads go through inb from <kernel/io.h>; KB_DATA_PORT is the
 *    only port touched.
 *  - keyboard_handle_irq runs in IRQ context, single-CPU (no SMP IRQ steering);
 *    modifier flags (shift/ctrl/caps/extended) are volatile and mutated only
 * there.
 *  - Both ringbufs (kb_ring, kb_event_ring) are written only from IRQ and read
 *    only from syscall context via kb_get_char/kb_event_pop/kb_external_push.
 *  - kb_external_push allows the serial RX path to inject ASCII into kb_ring.
 *
 * Not allowed:
 *  - Calling kmalloc, schedule, wait_*, vfs_* from this TU (IRQ context).
 *  - Exposing kb_ring/kb_event_ring storage or scancode LUTs outside the TU.
 *  - Calling process_* or signal_* from the IRQ handler.
 */

#include <kernel/interrupt.h>
#include <kernel/io.h>
#include <kernel/keyboard.h>
#include <kernel/kprintf.h>
#include <kernel/ports.h>
#include <kernel/ringbuf.h>
#include <stdint.h>

#define KB_BUFFER_SIZE 256

#define SCANCODE_EXTENDED 0xE0
#define SCANCODE_UP 0x48
#define SCANCODE_DOWN 0x50
#define SCANCODE_LEFT 0x4B
#define SCANCODE_RIGHT 0x4D

#define KEY_UP 0x80
#define KEY_DOWN 0x81
#define KEY_LEFT 0x82
#define KEY_RIGHT 0x83

static uint8_t kb_buffer_data[sizeof(ringbuf_t) + KB_BUFFER_SIZE];
static ringbuf_t *kb_ring = (ringbuf_t *)kb_buffer_data;

#define KB_EVENT_BUFFER_SIZE 256
static uint8_t kb_event_buffer_data[sizeof(ringbuf_t) + KB_EVENT_BUFFER_SIZE];
static ringbuf_t *kb_event_ring = (ringbuf_t *)kb_event_buffer_data;

#define KB_EVT_RELEASE 0x01
#define KB_EVT_EXTENDED 0x02

static void kb_event_push(uint8_t scancode, uint8_t flags) {
  ringbuf_push(kb_event_ring, scancode);
  ringbuf_push(kb_event_ring, flags);
}

int kb_event_pop(uint8_t *out_scancode, uint8_t *out_flags) {
  if (ringbuf_pop(kb_event_ring, out_scancode) < 0)
    return 0;
  if (ringbuf_pop(kb_event_ring, out_flags) < 0) {
    *out_flags = 0;
    return 0;
  }
  return 1;
}

static volatile int extended_scancode = 0;

static volatile uint8_t shift_pressed = 0;
static volatile uint8_t ctrl_pressed = 0;
static volatile uint8_t caps_lock = 0;

static volatile int raw_mode = 0;

#define SCANCODE_LSHIFT 0x2A
#define SCANCODE_RSHIFT 0x36
#define SCANCODE_CTRL 0x1D
#define SCANCODE_CAPSLOCK 0x3A

static const char scancode_to_ascii[128] = {
    0,   27,   '1',  '2', '3',  '4', '5', '6', '7', '8', '9', '0', '-',
    '=', '\b', '\t', 'q', 'w',  'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
    '[', ']',  '\n', 0,   'a',  's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',
    ';', '\'', '`',  0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',',
    '.', '/',  0,    '*', 0,    ' ', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   '7', '8', '9', '-', '4', '5', '6',
    '+', '1',  '2',  '3', '0',  '.', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0};

static const char scancode_to_ascii_shift[128] = {
    0,   27,   '!',  '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',
    '+', '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
    '{', '}',  '\n', 0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L',
    ':', '"',  '~',  0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<',
    '>', '?',  0,    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   '7', '8', '9', '-', '4', '5', '6',
    '+', '1',  '2',  '3', '0', '.', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0};

void kb_external_push(char c) { ringbuf_push(kb_ring, (uint8_t)c); }

void keyboard_init(void) {
  ringbuf_init(kb_ring, KB_BUFFER_SIZE);
  ringbuf_init(kb_event_ring, KB_EVENT_BUFFER_SIZE);
  extended_scancode = 0;
  shift_pressed = 0;
  ctrl_pressed = 0;
  caps_lock = 0;
  irq_register(IRQ_KEYBOARD, keyboard_handle_irq);
}

void keyboard_handle_irq(void) {
  uint8_t scancode = inb(KB_DATA_PORT);

  if (raw_mode) {
    kb_external_push(scancode);
    return;
  }

  if (scancode == SCANCODE_EXTENDED) {
    extended_scancode = 1;
    return;
  }

  uint8_t key_released = (scancode & 0x80) != 0;
  uint8_t key_code = scancode & 0x7F;

  {
    uint8_t flags = 0;
    if (key_released)
      flags |= KB_EVT_RELEASE;
    if (extended_scancode)
      flags |= KB_EVT_EXTENDED;
    kb_event_push(key_code, flags);
  }

  if (key_code == SCANCODE_LSHIFT || key_code == SCANCODE_RSHIFT) {
    shift_pressed = !key_released;
    return;
  }
  if (key_code == SCANCODE_CTRL) {
    ctrl_pressed = !key_released;
    return;
  }
  if (key_code == SCANCODE_CAPSLOCK && !key_released) {
    caps_lock = !caps_lock;
    return;
  }

  if (extended_scancode) {
    extended_scancode = 0;
    if (!key_released) {
      switch (key_code) {
      case SCANCODE_UP:
        kb_external_push(KEY_UP);
        break;
      case SCANCODE_DOWN:
        kb_external_push(KEY_DOWN);
        break;
      case SCANCODE_LEFT:
        kb_external_push(KEY_LEFT);
        break;
      case SCANCODE_RIGHT:
        kb_external_push(KEY_RIGHT);
        break;
      }
    }
    return;
  }

  if (!key_released) {
    char c;

    uint8_t use_shift = shift_pressed;

    if (use_shift) {
      c = scancode_to_ascii_shift[key_code];
    } else {
      c = scancode_to_ascii[key_code];
    }

    if (caps_lock) {
      if (c >= 'a' && c <= 'z') {
        c = c - 'a' + 'A';
      } else if (c >= 'A' && c <= 'Z') {
        c = c - 'A' + 'a';
      }
    }

    if (ctrl_pressed && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
      c = (c & 0x1F);
    }

    if (c != 0) {
      kb_external_push(c);
    }
  }
}

char kb_get_char(void) {
  while (ringbuf_empty(kb_ring)) {
    __asm__ volatile("sti; hlt");
  }
  uint8_t c;
  if (ringbuf_pop(kb_ring, &c) < 0) {
    return 0;
  }
  return (char)c;
}
