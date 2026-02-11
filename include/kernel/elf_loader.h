#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <kernel/process.h>

int32_t load_elf_process(const char* path, process_t* proc);
int32_t create_test_init_elf(void);
int32_t elf_loader_init(void);
void elf_loader_cleanup(void);

#endif 
