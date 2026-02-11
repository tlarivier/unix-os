#include <kernel/elf.h>
#include <kernel/vfs.h>
#include <kernel/constants.h>
#include <kernel/gdt.h>
#include <kernel/process.h>
#include <kernel/errno.h>
#include <kernel/kernel.h>
#include <kernel/memory.h>
#include <kernel/paging.h>

uint32_t current_elf_size = 0;

static uint32_t aslr_seed = 0;

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static void init_aslr_entropy(void) {
    if (aslr_seed == 0) {
        uint64_t tsc = rdtsc();
        aslr_seed = (uint32_t)(tsc ^ (tsc >> 32));
        
        aslr_seed ^= (uint32_t)(tsc >> 16) * 2654435761U;
        aslr_seed ^= (uint32_t)(tsc & 0xFFFF) * 1597334677U;
        
        if (aslr_seed == 0) aslr_seed = 0xDEADBEEF;
    }
}

static uint32_t generate_pie_base(void) {
    init_aslr_entropy();
    
    aslr_seed = aslr_seed * 1103515245 + 12345;
    
    uint32_t range = 0x30000000 / PAGE_SIZE_CONST;  
    uint32_t offset = (aslr_seed >> 12) % range;
    uint32_t base = 0x10000000 + (offset * PAGE_SIZE_CONST);
    
    base &= ~0xFFFF;
    
    return base;
}

static int32_t apply_pie_relocations(char* elf_data, Elf32_Ehdr* ehdr, 
                                      process_memory_t* mem __attribute__((unused)), 
                                      uint32_t base_addr) {
    Elf32_Phdr* phdrs = (Elf32_Phdr*)(elf_data + ehdr->e_phoff);
    
    Elf32_Dyn* dynamic = NULL;
    
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_DYNAMIC) {
            dynamic = (Elf32_Dyn*)(elf_data + phdrs[i].p_offset);
            break;
        }
    }
    
    if (!dynamic) {
        /* No dynamic section - static PIE or no relocations needed */
        return 0;
    }
    
    uint32_t rel_addr = 0, rel_size = 0;
    
    for (Elf32_Dyn* dyn = dynamic; dyn->d_tag != DT_NULL; dyn++) {
        if (dyn->d_tag == DT_REL) rel_addr = dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_RELSZ) rel_size = dyn->d_un.d_val;
    }
    
    if (rel_addr && rel_size) {
        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdrs[i].p_type == PT_LOAD &&
                rel_addr >= phdrs[i].p_vaddr &&
                rel_addr < phdrs[i].p_vaddr + phdrs[i].p_filesz) {
                
                uint32_t file_offset = phdrs[i].p_offset + (rel_addr - phdrs[i].p_vaddr);
                Elf32_Rel* rel = (Elf32_Rel*)(elf_data + file_offset);
                uint32_t num_rels = rel_size / sizeof(Elf32_Rel);
                
                for (uint32_t j = 0; j < num_rels; j++) {
                    uint32_t type = ELF32_R_TYPE(rel[j].r_info);
                    
                    if (type == R_386_RELATIVE) {
                        /* R_386_RELATIVE: The relocated value is base_addr + *offset
                         * We patch directly in the ELF data before it's copied to memory */
                        uint32_t rel_file_off = 0;
                        for (int k = 0; k < ehdr->e_phnum; k++) {
                            if (phdrs[k].p_type == PT_LOAD &&
                                rel[j].r_offset >= phdrs[k].p_vaddr &&
                                rel[j].r_offset < phdrs[k].p_vaddr + phdrs[k].p_filesz) {
                                rel_file_off = phdrs[k].p_offset + (rel[j].r_offset - phdrs[k].p_vaddr);
                                break;
                            }
                        }
                        if (rel_file_off) {
                            uint32_t* target = (uint32_t*)(elf_data + rel_file_off);
                            *target += base_addr;
                        }
                    }
                }
                break;
            }
        }
    }
    
    return 0;
}

static int32_t validate_elf_header(Elf32_Ehdr* header) {
    if (!header) return -EINVAL;
    
    if (header->e_ident[EI_MAG0] != ELFMAG0 ||
        header->e_ident[EI_MAG1] != ELFMAG1 ||
        header->e_ident[EI_MAG2] != ELFMAG2 ||
        header->e_ident[EI_MAG3] != ELFMAG3) {
        return -EINVAL;
    }
    if (header->e_ident[EI_CLASS] != ELFCLASS32) return -EINVAL;
    if (header->e_ident[EI_DATA] != ELFDATA2LSB) return -EINVAL;
    if (header->e_machine != EM_386) return -EINVAL;
    
    int is_pie = 0;
    if (header->e_type == ET_DYN) {
        is_pie = 1;
    } else if (header->e_type == ET_EXEC) {
        if (header->e_entry < USER_CODE_BASE || header->e_entry >= USER_SPACE_END) {
            return -EINVAL;
        }
    } else {
        return -EINVAL;  
    }
    
    if (header->e_phentsize != sizeof(Elf32_Phdr)) return -EINVAL;
    if (header->e_phnum > 32) return -EINVAL;
    if (header->e_shnum > 1024) return -EINVAL;
    if (header->e_version != EV_CURRENT) return -EINVAL;
    
    return is_pie;  
}

static int32_t map_elf_segments_at(Elf32_Ehdr* elf_header, char* elf_data, 
                                    process_memory_t* mem, uint32_t base_addr) {
    if (!elf_header || !elf_data || !mem) {
        KERNEL_ERROR(-EINVAL, "Invalid parameters to map_elf_segments");
        return -EINVAL;
    }
    
    if (elf_header->e_phoff == 0 || elf_header->e_phnum == 0) {
        KERNEL_ERROR(-EINVAL, "No program headers in ELF");
        return -EINVAL;
    }
    
    Elf32_Phdr* program_headers = (Elf32_Phdr*)(elf_data + elf_header->e_phoff);
    
    for (int i = 0; i < elf_header->e_phnum; i++) {
        Elf32_Phdr* phdr = &program_headers[i];
        
        if (phdr->p_type != PT_LOAD) {
            continue;
        }
        
        extern uint32_t current_elf_size;  
        if (phdr->p_filesz > 0 && 
            (phdr->p_offset > current_elf_size || 
             phdr->p_filesz > current_elf_size ||
             phdr->p_offset + phdr->p_filesz > current_elf_size)) {
            kprintf("ELF: segment invalid: offset=%x filesz=%x elf_size=%x\n",
                    phdr->p_offset, phdr->p_filesz, current_elf_size);
            KERNEL_ERROR(-EINVAL, "Invalid segment offset/size in ELF");
            return -EINVAL;
        }
        
        uint32_t actual_vaddr = phdr->p_vaddr + base_addr;
        
        if (actual_vaddr < 0x00400000 || actual_vaddr >= 0x80000000) {
            KERNEL_ERROR(-EFAULT, "Invalid segment virtual address");
            return -EFAULT;
        }
        
        if (phdr->p_memsz == 0) {
            continue; 
        }
        
        if (actual_vaddr + phdr->p_memsz < actual_vaddr) {
            KERNEL_ERROR(-EOVERFLOW, "Segment size overflow");
            return -EOVERFLOW;
        }
        
        uint32_t vaddr_start = actual_vaddr & ~(PAGE_SIZE_CONST - 1);  
        uint32_t vaddr_end   = (actual_vaddr + phdr->p_memsz + PAGE_SIZE_CONST - 1) & ~(PAGE_SIZE_CONST - 1);
        uint32_t num_pages   = (vaddr_end - vaddr_start) / PAGE_SIZE_CONST;
        
        if (num_pages > 1024) {  
            KERNEL_ERROR(-ENFILE, "Segment too large");
            return -ENFILE;
        }
        
        uint32_t page_flags = PAGE_PRESENT | PAGE_USER;
        if (phdr->p_flags & PF_W) {
            page_flags |= PAGE_WRITABLE;
        }
        
        char* segment_data = elf_data + phdr->p_offset;
        uint32_t segment_offset = actual_vaddr - vaddr_start;  
        uint32_t bytes_copied = 0;
        
        for (uint32_t page = 0; page < num_pages; page++) {
            uint32_t virtual_addr = vaddr_start + (page * PAGE_SIZE_CONST);
            uint32_t physical_addr = allocate_frame();
            
            if (!physical_addr || physical_addr == (uint32_t)-1) {
                KERNEL_ERROR(-ENOMEM, "Cannot allocate frame for segment");
                return -ENOMEM;
            }
            
            zero_frame(physical_addr);
            
            if (phdr->p_filesz > 0 && bytes_copied < phdr->p_filesz) {
                uint32_t page_start = (page == 0) ? segment_offset : 0;
                uint32_t bytes_to_copy = phdr->p_filesz - bytes_copied;
                if (bytes_to_copy > PAGE_SIZE_CONST - page_start) {
                    bytes_to_copy = PAGE_SIZE_CONST - page_start;
                }
                
                if (bytes_copied < 32) {  /* Only first few copies */
                }
                copy_to_frame(physical_addr, page_start, segment_data + bytes_copied, bytes_to_copy);
                bytes_copied += bytes_to_copy;
            }
            
            int32_t map_result = map_page_ext(mem->page_directory, virtual_addr, physical_addr, page_flags);
            if (IS_ERROR(map_result)) {
                free_frame(physical_addr);
                KERNEL_ERROR(-EIO, "Failed to map page");
                return -EIO;
            }
        }
    }
    
    return 0;
}

static int32_t load_interpreter(const char* interp_path, process_t* proc, 
                                 uint32_t* interp_base_out) {
    int32_t fd = vfs_open(interp_path, 0, 0644);
    if (fd < 0) {
        kprintf("Cannot open interpreter: %s\n", interp_path);
        return -ENOENT;
    }
    
    Elf32_Ehdr ehdr;
    if (vfs_read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        kprintf("load_interpreter: read header failed\n");
        vfs_close(fd);
        return -EIO;
    }
    
    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr.e_ident[EI_MAG2] != ELFMAG2 || ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        kprintf("load_interpreter: bad magic\n");
        vfs_close(fd);
        return -EINVAL;
    }
    if (ehdr.e_type != ET_DYN && ehdr.e_type != ET_EXEC) {
        kprintf("load_interpreter: not ET_DYN/ET_EXEC (type=%d)\n", ehdr.e_type);
        vfs_close(fd);
        return -EINVAL;
    }
    
    vfs_node_t* node = vfs_resolve_path(interp_path);
    if (!node) {
        vfs_close(fd);
        return -ENOENT;
    }
    uint32_t file_size = node->size;
    
    vfs_close(fd);
    fd = vfs_open(interp_path, 0, 0644);
    char* data = kmalloc(file_size + PAGE_SIZE_CONST);
    if (!data) {
        vfs_close(fd);
        return -ENOMEM;
    }
    vfs_read(fd, data, file_size);
    vfs_close(fd);
    
    uint32_t interp_base = proc->memory->mmap_next_addr;
    proc->memory->mmap_next_addr += 0x400000;
    *interp_base_out = interp_base;
    
    current_elf_size = file_size;
    
    int32_t result = map_elf_segments_at((Elf32_Ehdr*)data, data, proc->memory, interp_base);
    if (IS_ERROR(result)) {
        kprintf("load_interpreter: map failed %d\n", result);
        kfree(data);
        return result;
    }
    
    apply_pie_relocations(data, (Elf32_Ehdr*)data, proc->memory, interp_base);
    
    uint32_t entry = ehdr.e_entry + interp_base;
    
    kfree(data);
    return (int32_t)entry;
}

static int32_t map_segment_streaming(int32_t fd, Elf32_Phdr* phdr, 
                                      process_memory_t* mem, uint32_t base_addr) {
    if (phdr->p_type != PT_LOAD || phdr->p_memsz == 0) {
        return 0;  
    }
    
    uint32_t actual_vaddr = phdr->p_vaddr + base_addr;
    
    if (actual_vaddr < 0x00400000 || actual_vaddr >= 0x80000000) {
        return -EFAULT;
    }
    
    uint32_t vaddr_start = actual_vaddr & ~(PAGE_SIZE_CONST - 1);
    uint32_t vaddr_end   = (actual_vaddr + phdr->p_memsz + PAGE_SIZE_CONST - 1) & ~(PAGE_SIZE_CONST - 1);
    uint32_t num_pages   = (vaddr_end - vaddr_start) / PAGE_SIZE_CONST;
    
    if (num_pages > 1024) return -ENFILE;  /* Limit to 4MB per segment */
    
    uint32_t page_flags = PAGE_PRESENT | PAGE_USER;
    if (phdr->p_flags & PF_W) page_flags |= PAGE_WRITABLE;
    
    uint32_t segment_offset = actual_vaddr - vaddr_start;
    uint32_t file_bytes_remaining = phdr->p_filesz;
    uint32_t file_pos = phdr->p_offset;
    
    /* Seek to segment start in file */
    vfs_lseek(fd, file_pos, 0);
    
    for (uint32_t page = 0; page < num_pages; page++) {
        uint32_t virtual_addr = vaddr_start + (page * PAGE_SIZE_CONST);
        uint32_t physical_addr = allocate_frame();
        
        if (!physical_addr || physical_addr == (uint32_t)-1) {
            return -ENOMEM;
        }
        
        zero_frame(physical_addr);
        
        if (file_bytes_remaining > 0) {
            uint32_t page_start = (page == 0) ? segment_offset : 0;
            uint32_t bytes_to_read = file_bytes_remaining;
            if (bytes_to_read > PAGE_SIZE_CONST - page_start) {
                bytes_to_read = PAGE_SIZE_CONST - page_start;
            }
            
            if (bytes_to_read > 0) {
                uint8_t buf[512];
                uint32_t bytes_read_total = 0;
                
                while (bytes_read_total < bytes_to_read) {
                    uint32_t chunk = bytes_to_read - bytes_read_total;
                    if (chunk > 512) chunk = 512;
                    
                    ssize_t r = vfs_read(fd, buf, chunk);
                    if (r <= 0) break;
                    
                    copy_to_frame(physical_addr, page_start + bytes_read_total, buf, r);
                    bytes_read_total += r;
                }
                
                file_bytes_remaining -= bytes_read_total;
            }
        }
        
        if (map_page_ext(mem->page_directory, virtual_addr, physical_addr, page_flags) < 0) {
            free_frame(physical_addr);
            return -EIO;
        }
    }
    
    return 0;
}

int32_t load_elf_process(const char* path, process_t* proc) {
    if (!path || !proc || !proc->memory) {
        KERNEL_ERROR(-EINVAL, "Invalid parameters to load_elf_process");
        return -EINVAL;
    }
    
    int32_t path_result = validate_kernel_string(path, 256);
    if (IS_ERROR(path_result)) {
        KERNEL_ERROR(-EINVAL, "Invalid path string");
        return -EINVAL;
    }
    
    int32_t fd = vfs_open(path, 0, 0644);
    if (fd < 0) {
        KERNEL_ERROR(-ENOENT, "ELF file not found");
        return -ENOENT;
    }
    
    Elf32_Ehdr elf_header;
    ssize_t header_bytes = vfs_read(fd, &elf_header, sizeof(Elf32_Ehdr));
    if (header_bytes != (ssize_t)sizeof(Elf32_Ehdr)) {
        vfs_close(fd);
        KERNEL_ERROR(-EIO, "Failed to read ELF header");
        return -EIO;
    }
    
    int32_t validation_result = validate_elf_header(&elf_header);
    if (IS_ERROR(validation_result)) {
        vfs_close(fd);
        return validation_result;
    }
    int is_pie = validation_result;
    
    if (elf_header.e_phnum > 32) {
        vfs_close(fd);
        return -EINVAL;
    }
    
    Elf32_Phdr phdrs[32];
    vfs_lseek(fd, elf_header.e_phoff, 0);
    ssize_t ph_bytes = vfs_read(fd, phdrs, elf_header.e_phnum * sizeof(Elf32_Phdr));
    if (ph_bytes != (ssize_t)(elf_header.e_phnum * sizeof(Elf32_Phdr))) {
        vfs_close(fd);
        return -EIO;
    }
    
    uint32_t base_addr = 0;
    if (is_pie) {
        base_addr = generate_pie_base();
    }
    
    uint32_t brk_end = 0;
    for (int i = 0; i < elf_header.e_phnum; i++) {
        int32_t result = map_segment_streaming(fd, &phdrs[i], proc->memory, base_addr);
        if (IS_ERROR(result)) {
            vfs_close(fd);
            return result;
        }
        if (phdrs[i].p_type == PT_LOAD) {
            uint32_t seg_end = phdrs[i].p_vaddr + base_addr + phdrs[i].p_memsz;
            seg_end = (seg_end + PAGE_SIZE_CONST - 1) & ~(PAGE_SIZE_CONST - 1);
            if (seg_end > brk_end) brk_end = seg_end;
        }
    }
    
    if (brk_end > 0) {
        proc->memory->brk = brk_end;
    }
    
    const char* interp_path = NULL;
    for (int i = 0; i < elf_header.e_phnum; i++) {
        if (phdrs[i].p_type == PT_INTERP && phdrs[i].p_filesz > 0 && phdrs[i].p_filesz < 256) {
            static char interp_buf[256];
            vfs_lseek(fd, phdrs[i].p_offset, 0);
            ssize_t r = vfs_read(fd, interp_buf, phdrs[i].p_filesz);
            if (r > 0) {
                interp_buf[r] = '\0';
                interp_path = interp_buf;
            }
            break;
        }
    }
    
    vfs_close(fd);
    
    uint32_t entry_point = elf_header.e_entry + base_addr;
    
    if (interp_path) {
        /* Load dynamic linker */
        uint32_t interp_base = 0;
        int32_t interp_entry = load_interpreter(interp_path, proc, &interp_base);
        if (IS_ERROR(interp_entry)) {
            return interp_entry;
        }
        
        entry_point = (uint32_t)interp_entry;
        
        /* Store program info for ld.so */
        uint32_t load_base = 0;
        for (int i = 0; i < elf_header.e_phnum; i++) {
            if (phdrs[i].p_type == PT_LOAD && phdrs[i].p_offset == 0) {
                load_base = phdrs[i].p_vaddr;
                break;
            }
        }
        
        proc->elf_entry   = elf_header.e_entry + base_addr;
        proc->elf_phdr    = load_base + base_addr + elf_header.e_phoff;
        proc->elf_phnum   = elf_header.e_phnum;
        proc->interp_base = interp_base;
    }
    
    proc->context.eip = entry_point;
    
    proc->context.cs = USER_CODE_SEL;
    proc->context.ds = proc->context.es = proc->context.fs = proc->context.gs = USER_DATA_SEL;
    proc->context.ss = USER_DATA_SEL;
    
    if (proc->user_stack_base <= 0x08000000 || proc->user_stack_base > 0x80000000) {
        KERNEL_ERROR(-EFAULT, "Invalid user stack base");
        return -EFAULT;
    }
    proc->context.esp = proc->user_stack_base - 4;
    
    return 0;
}

int32_t elf_loader_init(void) {
    init_aslr_entropy();
    return 0;
}

void elf_loader_cleanup(void) {
    kprintf("ELF loader cleanup completed\n");
}
