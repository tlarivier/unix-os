#ifndef KERNEL_KEYBOARD_H
#define KERNEL_KEYBOARD_H

#include <stdint.h>

void keyboard_init(void);
void keyboard_handle_irq(void);
char kb_get_char(void);

void kb_external_push(char c);
int kb_event_pop(uint8_t *out_scancode, uint8_t *out_flags);

#endif
