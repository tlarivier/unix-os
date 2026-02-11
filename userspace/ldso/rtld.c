#include <stdint.h>
#include <stddef.h>
#include "rtld_syscall.h"

#define ELF_MAGIC    0x464C457F
#define ET_EXEC      2
#define ET_DYN       3
#define PT_NULL      0
#define PT_LOAD      1
#define PT_DYNAMIC   2
#define PT_INTERP    3

#define DT_NULL      0
#define DT_NEEDED    1
#define DT_PLTRELSZ  2
#define DT_PLTGOT    3
#define DT_HASH      4
#define DT_STRTAB    5
#define DT_SYMTAB    6
#define DT_STRSZ     10
#define DT_INIT      12
#define DT_FINI      13
#define DT_SONAME    14
#define DT_REL       17
#define DT_RELSZ     18
#define DT_JMPREL    23
#define DT_BIND_NOW  24
#define DT_VERSYM    0x6ffffff0
#define DT_VERDEF    0x6ffffffc
#define DT_VERDEFNUM 0x6ffffffd
#define DT_VERNEED   0x6ffffffe
#define DT_VERNEEDNUM 0x6fffffff

#define VER_NDX_LOCAL  0
#define VER_NDX_GLOBAL 1
#define VER_FLG_BASE   0x1
#define VER_FLG_WEAK   0x2

#define R_386_NONE      0
#define R_386_32        1
#define R_386_PC32      2
#define R_386_COPY      5
#define R_386_GLOB_DAT  6
#define R_386_JMP_SLOT  7
#define R_386_RELATIVE  8

#define STB_LOCAL   0
#define STB_GLOBAL  1
#define STB_WEAK    2
#define SHN_UNDEF   0

#define ELF32_ST_BIND(i)  ((i) >> 4)
#define ELF32_R_SYM(i)    ((i) >> 8)
#define ELF32_R_TYPE(i)   ((i) & 0xff)

#define PF_X 1
#define PF_W 2
#define PF_R 4

typedef struct {
    uint32_t e_ident_magic;
    uint8_t  e_ident_rest[12];
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
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

typedef struct {
    int32_t d_tag;
    uint32_t d_val;
} Elf32_Dyn;

typedef struct {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
} Elf32_Sym;

typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
} Elf32_Rel;

static size_t rtld_strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

static int rtld_strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

static void *rtld_memcpy(void *dest, const void *src, size_t n) {
    char *d = dest;
    const char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

static void *rtld_memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

static void rtld_strcpy(char *dest, const char *src) {
    while ((*dest++ = *src++));
}

static void rtld_puts(const char *s) {
    rtld_write(2, s, rtld_strlen(s));
    rtld_write(2, "\n", 1);
}

static uint32_t elf_hash(const char *name) {
    uint32_t h = 0, g;
    while (*name) {
        h = (h << 4) + (unsigned char)*name++;
        g = h & 0xf0000000;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

#define MAX_LIBS   32
#define MAX_NEEDED 16

typedef struct {
    uint16_t vd_version;
    uint16_t vd_flags;
    uint16_t vd_ndx;
    uint16_t vd_cnt;
    uint32_t vd_hash;
    uint32_t vd_aux;
    uint32_t vd_next;
} Elf32_Verdef;

typedef struct {
    uint32_t vda_name;
    uint32_t vda_next;
} Elf32_Verdaux;

typedef struct loaded_lib {
    char name[64];
    char soname[64];
    uint32_t base;
    uint32_t size;
    Elf32_Dyn *dynamic;
    Elf32_Sym *symtab;
    const char *strtab;
    uint32_t *hash;
    Elf32_Rel *rel;
    uint32_t relsz;
    Elf32_Rel *pltrel;
    uint32_t pltrelsz;
    uint32_t *pltgot;
    void (*init)(void);
    void (*fini)(void);
    int refcount;
    int loaded;
    char needed[MAX_NEEDED][64];
    int needed_count;
    uint16_t *versym;           
    Elf32_Verdef *verdef;       
    uint32_t verdefnum;         
} loaded_lib_t;

static loaded_lib_t libs[MAX_LIBS];
static int num_libs = 0;
static uint32_t next_lib_base = 0x20000000;  
static int bind_now = 0;

static loaded_lib_t *load_library_internal(const char *path, int depth);
static int process_relocations(loaded_lib_t *lib);

static Elf32_Sym *find_symbol_in_lib(loaded_lib_t *lib, const char *name) {
    if (!lib->symtab || !lib->strtab || !lib->hash) return NULL;
    
    uint32_t nbucket =  lib->hash[0];
    uint32_t nchain  =  lib->hash[1];
    uint32_t *bucket = &lib->hash[2];
    uint32_t *chain  = &lib->hash[2 + nbucket];
    
    uint32_t h = elf_hash(name) % nbucket;
    for (uint32_t i = bucket[h]; i != 0 && i < nchain; i = chain[i]) {
        Elf32_Sym *sym = &lib->symtab[i];
        if (sym->st_shndx == SHN_UNDEF) continue;
        if (rtld_strcmp(lib->strtab + sym->st_name, name) == 0) {
            return sym;
        }
    }
    return NULL;
}

static uint32_t lookup_symbol(const char *name) {
    for (int i = 0; i < num_libs; i++) {
        if (!libs[i].loaded) continue;
        Elf32_Sym *sym = find_symbol_in_lib(&libs[i], name);
        if (sym && sym->st_value) {
            return libs[i].base + sym->st_value;
        }
    }
    return 0;
}

/* Lazy binding resolver */
uint32_t __rtld_resolve(loaded_lib_t *lib, uint32_t reloc_idx) {
    if (!lib->pltrel || reloc_idx * sizeof(Elf32_Rel) >= lib->pltrelsz) {
        return 0;
    }
    
    Elf32_Rel *rel = &lib->pltrel[reloc_idx];
    uint32_t sym_idx = ELF32_R_SYM(rel->r_info);
    
    if (sym_idx != 0 && lib->symtab && lib->strtab) {
        Elf32_Sym *sym = &lib->symtab[sym_idx];
        const char *sym_name = lib->strtab + sym->st_name;
        uint32_t addr = lookup_symbol(sym_name);
        
        if (addr) {
            uint32_t *got_entry = (uint32_t *)(lib->base + rel->r_offset);
            *got_entry = addr;
            return addr;
        }
    }
    return 0;
}

static int process_relocations(loaded_lib_t *lib) {
    if (lib->rel && lib->relsz) {
        uint32_t count = lib->relsz / sizeof(Elf32_Rel);
        for (uint32_t i = 0; i < count; i++) {
            Elf32_Rel *rel = &lib->rel[i];
            uint32_t *target = (uint32_t *)(lib->base + rel->r_offset);
            uint32_t sym_idx = ELF32_R_SYM(rel->r_info);
            uint32_t type = ELF32_R_TYPE(rel->r_info);
            
            uint32_t sym_addr = 0;
            if (sym_idx != 0 && lib->symtab && lib->strtab) {
                Elf32_Sym *sym = &lib->symtab[sym_idx];
                const char *sym_name = lib->strtab + sym->st_name;
                sym_addr = lookup_symbol(sym_name);
                if (!sym_addr && ELF32_ST_BIND(sym->st_info) != STB_WEAK) {
                    rtld_puts("ld.so: undefined symbol");
                    return -1;
                }
            }
            
            switch (type) {
                case R_386_NONE: break;
                case R_386_32: *target += sym_addr; break;
                case R_386_PC32: *target += sym_addr - (uint32_t)target; break;
                case R_386_GLOB_DAT: *target = sym_addr; break;
                case R_386_RELATIVE: *target += lib->base; break;
                case R_386_COPY:
                    if (sym_addr) {
                        Elf32_Sym *sym = &lib->symtab[sym_idx];
                        rtld_memcpy(target, (void*)sym_addr, sym->st_size);
                    }
                    break;
            }
        }
    }
    
    /*
     * For now, always resolve immediately (no lazy binding).
     * Lazy binding requires PLT stubs that call back to ld.so,
     * which is more complex to set up.
     */
    if (lib->pltrel && lib->pltrelsz) {
        uint32_t count = lib->pltrelsz / sizeof(Elf32_Rel);
        for (uint32_t i = 0; i < count; i++) {
            Elf32_Rel *rel = &lib->pltrel[i];
            uint32_t *target = (uint32_t *)(lib->base + rel->r_offset);
            uint32_t sym_idx = ELF32_R_SYM(rel->r_info);
            
            if (sym_idx != 0 && lib->symtab && lib->strtab) {
                Elf32_Sym *sym = &lib->symtab[sym_idx];
                const char *sym_name = lib->strtab + sym->st_name;
                uint32_t sym_addr = lookup_symbol(sym_name);
                if (sym_addr) {
                    *target = sym_addr;
                }
            }
        }
    }
    
    return 0;
}

static void parse_dynamic(loaded_lib_t *lib) {
    if (!lib->dynamic) return;
    
    lib->needed_count = 0;
    
    for (Elf32_Dyn *dyn = lib->dynamic; dyn->d_tag != DT_NULL; dyn++) {
        switch (dyn->d_tag) {
            case DT_SYMTAB:   lib->symtab   = (Elf32_Sym *)(lib->base + dyn->d_val);   break;
            case DT_STRTAB:   lib->strtab   = (const char *)(lib->base + dyn->d_val);  break;
            case DT_HASH:     lib->hash     = (uint32_t *)(lib->base + dyn->d_val);    break;
            case DT_REL:      lib->rel      = (Elf32_Rel *)(lib->base + dyn->d_val);   break;
            case DT_RELSZ:    lib->relsz    = dyn->d_val;                              break;
            case DT_JMPREL:   lib->pltrel   = (Elf32_Rel *)(lib->base + dyn->d_val);   break;
            case DT_PLTRELSZ: lib->pltrelsz = dyn->d_val;                              break;
            case DT_PLTGOT:   lib->pltgot   = (uint32_t *)(lib->base + dyn->d_val);    break;
            case DT_INIT:     lib->init     = (void(*)(void))(lib->base + dyn->d_val); break;
            case DT_FINI:     lib->fini     = (void(*)(void))(lib->base + dyn->d_val); break;
            case DT_BIND_NOW: bind_now = 1; break;
        }
    }
    
    if (lib->strtab) {
        lib->needed_count = 0;
        for (Elf32_Dyn *dyn = lib->dynamic; dyn->d_tag != DT_NULL; dyn++) {
            if (dyn->d_tag == DT_NEEDED && lib->needed_count < MAX_NEEDED) {
                rtld_strcpy(lib->needed[lib->needed_count], lib->strtab + dyn->d_val);
                lib->needed_count++;
            } else if (dyn->d_tag == DT_SONAME) {
                rtld_strcpy(lib->soname, lib->strtab + dyn->d_val);
            }
        }
    }
}

static int load_dependencies(loaded_lib_t *lib, int depth) {
    if (depth > 10) return -1;
    
    for (int i = 0; i < lib->needed_count; i++) {
        const char *name = lib->needed[i];
        if (name[0] == '\0') continue;
        
        int found = 0;
        for (int j = 0; j < num_libs; j++) {
            if (rtld_strcmp(libs[j].name, name) == 0 ||
                rtld_strcmp(libs[j].soname, name) == 0) {
                libs[j].refcount++;
                found = 1;
                break;
            }
        }
        
        if (!found) {
            char path[128] = "/lib/";
            int k = 5;
            const char *n = name;
            while (*n && k < 127) path[k++] = *n++;
            path[k] = '\0';
            
            if (!load_library_internal(path, depth + 1)) {
                return -1;
            }
        }
    }
    return 0;
}

static loaded_lib_t *load_library_internal(const char *path, int depth) {
    if (num_libs >= MAX_LIBS) return NULL;
    
    for (int i = 0; i < num_libs; i++) {
        if (rtld_strcmp(libs[i].name, path) == 0) {
            libs[i].refcount++;
            return &libs[i];
        }
    }
    
    int fd = rtld_open(path, 0, 0);
    if (fd < 0) {
        rtld_puts("rtld: open failed");
        return NULL;
    }
    
    /* Use static buffer to avoid stack issues */
    static Elf32_Ehdr ehdr;
    rtld_memset(&ehdr, 0, sizeof(ehdr));
    
    int rd = rtld_read(fd, &ehdr, sizeof(ehdr));
    if (rd != (int)sizeof(ehdr)) {
        rtld_close(fd);
        return NULL;
    }
    
    if (ehdr.e_ident_magic != ELF_MAGIC || ehdr.e_type != ET_DYN) {
        rtld_close(fd);
        return NULL;
    }
    
    loaded_lib_t *lib = &libs[num_libs];
    rtld_memset(lib, 0, sizeof(*lib));
    rtld_strcpy(lib->name, path);
    lib->base = next_lib_base;
    lib->refcount = 1;
    
    /* Use static buffer for phdr (same stack issue as ehdr) */
    static Elf32_Phdr phdr;
    
    uint32_t max_addr = 0;
    for (int i = 0; i < ehdr.e_phnum; i++) {
        rtld_memset(&phdr, 0, sizeof(phdr));
        rtld_lseek(fd, ehdr.e_phoff + i * sizeof(Elf32_Phdr), 0);
        if (rtld_read(fd, &phdr, sizeof(phdr)) != (int)sizeof(phdr)) continue;
        
        if (phdr.p_type == PT_LOAD) {
            uint32_t end = phdr.p_vaddr + phdr.p_memsz;
            if (end > max_addr) max_addr = end;
            
            int prot = 0;
            if (phdr.p_flags & PF_R) prot |= PROT_READ;
            if (phdr.p_flags & PF_W) prot |= PROT_WRITE;
            if (phdr.p_flags & PF_X) prot |= PROT_EXEC;
            
            uint32_t addr = lib->base + (phdr.p_vaddr & ~0xFFF);
            uint32_t size = ((phdr.p_vaddr & 0xFFF) + phdr.p_memsz + 0xFFF) & ~0xFFF;
            
            void *mem = rtld_mmap((void*)addr, size, prot | PROT_WRITE,
                                  MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
            if (mem == (void*)-1 || mem != (void*)addr) {
                rtld_close(fd);
                return NULL;
            }
            
            if (phdr.p_filesz > 0) {
                static uint8_t read_buf[4096];
                rtld_lseek(fd, phdr.p_offset, 0);
                uint32_t remaining = phdr.p_filesz;
                uint32_t dest = lib->base + phdr.p_vaddr;
                while (remaining > 0) {
                    uint32_t chunk = remaining > sizeof(read_buf) ? sizeof(read_buf) : remaining;
                    int rd = rtld_read(fd, read_buf, chunk);
                    if (rd <= 0) break;
                    rtld_memcpy((void*)dest, read_buf, rd);
                    dest += rd;
                    remaining -= rd;
                }
            }
        } else if (phdr.p_type == PT_DYNAMIC) {
            lib->dynamic = (Elf32_Dyn *)(lib->base + phdr.p_vaddr);
        }
    }
    
    lib->size = (max_addr + 0xFFF) & ~0xFFF;
    lib->loaded = 1;
    
    rtld_close(fd);
    
    next_lib_base += lib->size + 0x10000;
    num_libs++;
    
    parse_dynamic(lib);
    if (load_dependencies(lib, depth) < 0) return NULL;
    if (process_relocations(lib) < 0) return NULL;
    
    if (lib->pltgot && !bind_now) {
        lib->pltgot[1] = (uint32_t)lib;
        lib->pltgot[2] = (uint32_t)__rtld_resolve;
    }
    
    return lib;
}

/* Public API */
void *__rtld_dlopen(const char *filename, int flags) {
    (void)flags;
    if (!filename) return (void*)1;
    
    char path[128];
    if (filename[0] == '/') {
        rtld_strcpy(path, filename);
    } else {
        rtld_strcpy(path, "/lib/");
        int i = 5;
        while (*filename && i < 127) path[i++] = *filename++;
        path[i] = '\0';
    }
    
    loaded_lib_t *lib = load_library_internal(path, 0);
    if (!lib) return NULL;
    if (lib->init) lib->init();
    return lib;
}

void *__rtld_dlsym(void *handle, const char *symbol) {
    if (!symbol) return NULL;
    if (handle == (void*)1 || handle == NULL) {
        return (void*)lookup_symbol(symbol);
    }
    loaded_lib_t *lib = (loaded_lib_t *)handle;
    Elf32_Sym *sym = find_symbol_in_lib(lib, symbol);
    if (sym && sym->st_value) {
        return (void*)(lib->base + sym->st_value);
    }
    return NULL;
}

int __rtld_dlclose(void *handle) {
    if (!handle || handle == (void*)1) return 0;
    loaded_lib_t *lib = (loaded_lib_t *)handle;
    lib->refcount--;
    if (lib->refcount <= 0) {
        if (lib->fini) lib->fini();
        if (lib->base && lib->size) {
            rtld_munmap((void*)lib->base, lib->size);
        }
        lib->loaded = 0;
    }
    return 0;
}

#define AT_NULL   0
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_BASE   7
#define AT_ENTRY  9


#ifndef RTLD_EMBEDDED

static loaded_lib_t *setup_main_executable(Elf32_Phdr *phdr, uint32_t phnum, uint32_t base) {
    if (num_libs >= MAX_LIBS) return NULL;
    
    loaded_lib_t *lib = &libs[num_libs];
    rtld_memset(lib, 0, sizeof(*lib));
    rtld_strcpy(lib->name, "[main]");
    lib->base = base;
    lib->refcount = 1;
    lib->loaded = 1;
    
    uint32_t max_addr = 0;
    for (uint32_t i = 0; i < phnum; i++) {
        if (phdr[i].p_type == PT_DYNAMIC) {
            lib->dynamic = (Elf32_Dyn *)(base + phdr[i].p_vaddr);
        } else if (phdr[i].p_type == PT_LOAD) {
            uint32_t end = phdr[i].p_vaddr + phdr[i].p_memsz;
            if (end > max_addr) max_addr = end;
        }
    }
    lib->size = max_addr;
    
    num_libs++;
    
    if (lib->dynamic) {
        parse_dynamic(lib);
    }
    
    return lib;
}

void _start_c(uint32_t* sp) {
    /* Stack layout: [argc] [argv...] [NULL] [envp...] [NULL] [auxv...] */
    int argc = (int)*sp++;
    char** argv = (char**)sp;
    sp += argc + 1;  /* Skip argv + NULL */
    
    char** envp = (char**)sp;
    while (*sp) sp++;  /* Skip envp */
    sp++;  /* Skip NULL */
    
    uint32_t* auxv = sp;
    uint32_t at_entry = 0;
    uint32_t at_phdr = 0;
    uint32_t at_phnum = 0;
    
    for (int i = 0; auxv[i] != AT_NULL && i < 30; i += 2) {
        switch (auxv[i]) {
            case AT_ENTRY: at_entry = auxv[i+1]; break;
            case AT_PHDR:  at_phdr = auxv[i+1]; break;
            case AT_PHNUM: at_phnum = auxv[i+1]; break;
        }
    }
    
    if (!at_entry) {
        rtld_puts("ld.so: no AT_ENTRY - standalone mode");
        rtld_exit(0);
    }
    
    /*
     * For non-PIE executables (loaded at fixed address like 0x08048000),
     * all addresses in the ELF are already absolute - base is 0.
     * 
     * For PIE executables, the kernel loads at a random address and
     * we need to calculate the base from AT_PHDR.
     */
    uint32_t prog_base = 0;
    if (at_entry < 0x08000000) {
        /* PIE: calculate base from phdr address minus standard offset */
        prog_base = at_phdr - 0x34;
    }
    /* Non-PIE: prog_base stays 0, addresses are absolute */
    
    loaded_lib_t *main_lib = NULL;
    if (at_phdr && at_phnum) {
        main_lib = setup_main_executable((Elf32_Phdr *)at_phdr, at_phnum, prog_base);
    }
    
    if (main_lib && main_lib->needed_count > 0) {
        if (load_dependencies(main_lib, 0) < 0) {
            rtld_puts("ld.so: failed to load dependencies");
            rtld_exit(1);
        }
    }
    
    if (main_lib && main_lib->dynamic) {
        if (process_relocations(main_lib) < 0) {
            rtld_puts("ld.so: relocation failed");
            rtld_exit(1);
        }
    }
    
    for (int i = num_libs - 1; i >= 0; i--) {
        if (libs[i].loaded && libs[i].init) {
            libs[i].init();
        }
    }
    
    void (*entry)(int, char**, char**) = (void(*)(int, char**, char**))at_entry;
    entry(argc, argv, envp);
    
    for (int i = 0; i < num_libs; i++) {
        if (libs[i].loaded && libs[i].fini) {
            libs[i].fini();
        }
    }
    
    rtld_exit(0);
}
#endif
