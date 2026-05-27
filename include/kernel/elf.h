#ifndef KERNEL_ELF_H
#define KERNEL_ELF_H

#include <stddef.h>
#include <stdint.h>

#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define EI_DATA 5
#define EI_NIDENT 16

#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ELFCLASS32 1

#define ELFDATA2LSB 1

#define ET_EXEC 2
#define ET_DYN 3

#define EM_386 3

#define PT_LOAD 1
#define PT_INTERP 3

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

typedef struct elf32_ehdr {
  uint8_t e_ident[EI_NIDENT];
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint32_t e_entry;
  uint32_t e_phoff;
  uint32_t e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
} __attribute__((packed)) Elf32_Ehdr;

typedef struct elf32_phdr {
  uint32_t p_type;
  uint32_t p_offset;
  uint32_t p_vaddr;
  uint32_t p_paddr;
  uint32_t p_filesz;
  uint32_t p_memsz;
  uint32_t p_flags;
  uint32_t p_align;
} __attribute__((packed)) Elf32_Phdr;

typedef struct {
  uint32_t a_type;
  uint32_t a_val;
} Elf32_auxv_t;

#endif /* KERNEL_ELF_H */
