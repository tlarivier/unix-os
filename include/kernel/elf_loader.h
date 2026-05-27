#ifndef KERNEL_ELF_LOADER_H
#define KERNEL_ELF_LOADER_H

#include <kernel/process.h>

#define ELF_PHENT_SIZE 32U

#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_PAGESZ 6
#define AT_BASE 7
#define AT_ENTRY 9

int32_t load_elf_process(const char *path, process_t *proc);
int32_t load_elf_process_into(const char *path, process_t *proc,
                              process_memory_t *mem_override);

#endif /* KERNEL_ELF_LOADER_H */
