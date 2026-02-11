#include "kernel/gdt.h"
#include "kernel/constants.h"

#define GDT_ENTRIES 6
static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t gdt_ptr;
static tss_entry_t tss;

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;
    
    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access      = access;
}

void gdt_init(void) {
    gdt_ptr.limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) - 1;
    gdt_ptr.base = (uint32_t)&gdt;
    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);
    gdt_flush((uint32_t)&gdt_ptr);
}

void gdt_setup_user_segments(void) { }

void tss_init(void) {
    uint32_t base = (uint32_t)&tss;
    uint32_t limit = sizeof(tss_entry_t);
    gdt_set_gate(5, base, limit, 0x89, 0x00);
    for (size_t i = 0; i < sizeof(tss_entry_t); i++) ((uint8_t*)&tss)[i] = 0;
    tss.ss0 = KERNEL_DATA_SEL;
    tss.esp0 = 0;
    tss.cs = KERNEL_CODE_SEL | 3;
    tss.ss = tss.ds = tss.es = tss.fs = tss.gs = KERNEL_DATA_SEL | 3;
    tss_flush();
}

void tss_set_kernel_stack(uint32_t esp0) {
    tss.esp0 = esp0;
}

uint32_t tss_get_esp0(void) {
    return tss.esp0;
}
