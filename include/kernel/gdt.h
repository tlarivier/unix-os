#ifndef GDT_H
#define GDT_H

#include <stddef.h>
#include <stdint.h>

#define GDT_FIRST_GS_SEL 0x68

void gdt_init(void);
void tss_init_bsp(void);
void tss_set_kernel_stack(uint32_t esp0);

void gdt_load_bsp_pointer(void);
void gs_load_for_cpu(uint32_t cpu_id);
void tr_load_for_cpu(uint32_t cpu_id);

#endif
