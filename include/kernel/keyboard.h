#ifndef KERNEL_KEYBOARD_H
#define KERNEL_KEYBOARD_H

#include <stdint.h>

void keyboard_init(void);
void keyboard_handle_irq(void);
int kb_has_char(void);
char kb_get_char(void);
char kb_try_get_char(void);

int keyboard_set_raw_mode(int enable);
int keyboard_get_raw_mode(void);

#endif 
