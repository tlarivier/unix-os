#ifndef ELF_H
#define ELF_H

#include <stdint.h>
#include <stddef.h>

// ELF identification
#define EI_MAG0     0
#define EI_MAG1     1  
#define EI_MAG2     2
#define EI_MAG3     3
#define EI_CLASS    4
#define EI_DATA     5
#define EI_VERSION  6
#define EI_NIDENT   16

// ELF magic numbers
#define ELFMAG0     0x7f
#define ELFMAG1     'E'
#define ELFMAG2     'L'
#define ELFMAG3     'F'

// ELF classes
#define ELFCLASSNONE 0
#define ELFCLASS32   1
#define ELFCLASS64   2

// ELF data encodings
#define ELFDATANONE  0
#define ELFDATA2LSB  1
#define ELFDATA2MSB  2

// ELF file types
#define ET_NONE      0
#define ET_REL       1
#define ET_EXEC      2
#define ET_DYN       3
#define ET_CORE      4

// ELF machine types
#define EM_NONE      0
#define EM_M32       1
#define EM_SPARC     2
#define EM_386       3
#define EM_68K       4
#define EM_88K       5
#define EM_860       7
#define EM_MIPS      8

// ELF versions
#define EV_NONE      0
#define EV_CURRENT   1

// Program header types
#define PT_NULL      0
#define PT_LOAD      1
#define PT_DYNAMIC   2
#define PT_INTERP    3
#define PT_NOTE      4
#define PT_SHLIB     5
#define PT_PHDR      6

// Program header flags
#define PF_X         0x1
#define PF_W         0x2
#define PF_R         0x4

typedef struct elf32_ehdr {
    uint8_t  e_ident[EI_NIDENT];  // ELF identification
    uint16_t e_type;               // Object file type
    uint16_t e_machine;            // Machine type
    uint32_t e_version;            // Object file version
    uint32_t e_entry;              // Entry point address
    uint32_t e_phoff;              // Program header offset
    uint32_t e_shoff;              // Section header offset
    uint32_t e_flags;              // Processor-specific flags
    uint16_t e_ehsize;             // ELF header size
    uint16_t e_phentsize;          // Program header entry size
    uint16_t e_phnum;              // Number of program header entries
    uint16_t e_shentsize;          // Section header entry size
    uint16_t e_shnum;              // Number of section header entries
    uint16_t e_shstrndx;           // String table section header index
} __attribute__((packed)) Elf32_Ehdr;

typedef struct elf32_phdr {
    uint32_t p_type;               // Segment type
    uint32_t p_offset;             // Segment file offset
    uint32_t p_vaddr;              // Segment virtual address
    uint32_t p_paddr;              // Segment physical address
    uint32_t p_filesz;             // Segment size in file
    uint32_t p_memsz;              // Segment size in memory
    uint32_t p_flags;              // Segment flags
    uint32_t p_align;              // Segment alignment
} __attribute__((packed)) Elf32_Phdr;

typedef struct elf32_shdr {
    uint32_t sh_name;              // Section name string table index
    uint32_t sh_type;              // Section type
    uint32_t sh_flags;             // Section flags
    uint32_t sh_addr;              // Section virtual address
    uint32_t sh_offset;            // Section file offset
    uint32_t sh_size;              // Section size
    uint32_t sh_link;              // Section link information
    uint32_t sh_info;              // Section extra information
    uint32_t sh_addralign;         // Section address alignment
    uint32_t sh_entsize;           // Section entry size
} __attribute__((packed)) Elf32_Shdr;

typedef struct elf32_dyn {
    int32_t  d_tag;
    union {
        uint32_t d_val;
        uint32_t d_ptr;
    } d_un;
} __attribute__((packed)) Elf32_Dyn;

typedef struct elf32_sym {
    uint32_t st_name;      /* Symbol name (string table index) */
    uint32_t st_value;     /* Symbol value */
    uint32_t st_size;      /* Symbol size */
    uint8_t  st_info;      /* Symbol type and binding */
    uint8_t  st_other;     /* Symbol visibility */
    uint16_t st_shndx;     /* Section index */
} __attribute__((packed)) Elf32_Sym;

typedef struct elf32_rel {
    uint32_t r_offset;     /* Address */
    uint32_t r_info;       /* Relocation type and symbol index */
} __attribute__((packed)) Elf32_Rel;

typedef struct elf32_rela {
    uint32_t r_offset;
    uint32_t r_info;
    int32_t  r_addend;
} __attribute__((packed)) Elf32_Rela;

#define DT_NULL         0
#define DT_NEEDED       1
#define DT_PLTRELSZ     2
#define DT_PLTGOT       3
#define DT_HASH         4
#define DT_STRTAB       5
#define DT_SYMTAB       6
#define DT_RELA         7
#define DT_RELASZ       8
#define DT_RELAENT      9
#define DT_STRSZ        10
#define DT_SYMENT       11
#define DT_INIT         12
#define DT_FINI         13
#define DT_SONAME       14
#define DT_RPATH        15
#define DT_SYMBOLIC     16
#define DT_REL          17
#define DT_RELSZ        18
#define DT_RELENT       19
#define DT_PLTREL       20
#define DT_DEBUG        21
#define DT_TEXTREL      22
#define DT_JMPREL       23

#define R_386_NONE      0
#define R_386_32        1
#define R_386_PC32      2
#define R_386_GOT32     3
#define R_386_PLT32     4
#define R_386_COPY      5
#define R_386_GLOB_DAT  6
#define R_386_JMP_SLOT  7
#define R_386_RELATIVE  8
#define R_386_GOTOFF    9
#define R_386_GOTPC     10

#define STB_LOCAL       0
#define STB_GLOBAL      1
#define STB_WEAK        2

#define STT_NOTYPE      0
#define STT_OBJECT      1
#define STT_FUNC        2
#define STT_SECTION     3
#define STT_FILE        4

#define ELF32_ST_BIND(i)    ((i) >> 4)
#define ELF32_ST_TYPE(i)    ((i) & 0xf)
#define ELF32_ST_INFO(b,t)  (((b) << 4) + ((t) & 0xf))

#define ELF32_R_SYM(i)      ((i) >> 8)
#define ELF32_R_TYPE(i)     ((uint8_t)(i))
#define ELF32_R_INFO(s,t)   (((s) << 8) + (uint8_t)(t))

#define SHT_NULL        0
#define SHT_PROGBITS    1
#define SHT_SYMTAB      2
#define SHT_STRTAB      3
#define SHT_RELA        4
#define SHT_HASH        5
#define SHT_DYNAMIC     6
#define SHT_NOTE        7
#define SHT_NOBITS      8
#define SHT_REL         9
#define SHT_SHLIB       10
#define SHT_DYNSYM      11

#define AT_NULL         0
#define AT_PHDR         3
#define AT_PHENT        4
#define AT_PHNUM        5
#define AT_PAGESZ       6
#define AT_BASE         7
#define AT_ENTRY        9

typedef struct {
    uint32_t a_type;
    uint32_t a_val;
} Elf32_auxv_t;

#endif 
