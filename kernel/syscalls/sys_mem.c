#include "syscall.h"
#include <kernel/memory.h>
#include <kernel/paging.h>
#include <kernel/constants.h>
#include <kernel/hashtable.h>
#include <kernel/spinlock.h>

#define ALLOC_FRAME_FAILED ((uint32_t)-1)

#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20
#define MAP_ANON        MAP_ANONYMOUS

#define PROT_NONE       0x0
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4

#undef PAGE_MASK
#define PAGE_MASK        (~(PAGE_SIZE_CONST - 1))
#define MAX_HEAP_SIZE    HEAP_MAX_SIZE
#define MMAP_BASE        USER_HEAP_BASE

int32_t sys_brk(uint32_t addr, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    
    process_t* cur = get_current_process();
    if (!cur || !cur->memory) return -ENOMEM;
    
    if (addr == 0) {
        return (int32_t)cur->memory->brk;
    }
    
    if (addr < HEAP_START) {
        return -EINVAL;
    }
    
    if (addr > HEAP_START + MAX_HEAP_SIZE) {
        return -ENOMEM;
    }
    
    uint32_t old_brk = cur->memory->brk;
    uint32_t new_brk = addr;
    
    if (new_brk > old_brk) {
        if (old_brk > UINT32_MAX - PAGE_SIZE_CONST) return -EINVAL;
        if (new_brk > UINT32_MAX - PAGE_SIZE_CONST) return -EINVAL;
        
        uint32_t start_page = (old_brk + PAGE_SIZE_CONST - 1) & PAGE_MASK;
        uint32_t end_page   = (new_brk + PAGE_SIZE_CONST - 1) & PAGE_MASK;
        
        for (uint32_t page = start_page; page < end_page; page += PAGE_SIZE_CONST) {
            uint32_t frame = allocate_frame();
            if (frame == ALLOC_FRAME_FAILED) return -ENOMEM;
            
            uint32_t flags = PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
            map_page_ext(cur->memory->page_directory, page, frame, flags);
        }
    }
    else if (new_brk < old_brk) {
        uint32_t start_page = (new_brk + PAGE_SIZE_CONST - 1) & PAGE_MASK;
        uint32_t end_page = (old_brk + PAGE_SIZE_CONST - 1) & PAGE_MASK;
        
        for (uint32_t page = start_page; page < end_page; page += PAGE_SIZE_CONST) {
            uint32_t phys = get_physical_addr(cur->memory->page_directory, page);
            if (phys) {
                unmap_page_ext(cur->memory->page_directory, page);
                free_frame(phys);
            }
        }
    }
    
    cur->memory->brk = new_brk;
    return (int32_t)new_brk;
}

static int is_fb_device(int fd) {
    if (fd < 0) return 0;
    extern const char* vfs_get_path_by_fd(int fd);
    const char* path = vfs_get_path_by_fd(fd);
    if (!path) return 0;
    if (path[0] == '/' && path[1] == 'd' && path[2] == 'e' && path[3] == 'v' && path[4] == '/') {
        if (path[5] == 'f' && path[6] == 'b' && (path[7] == '0' || path[7] == '\0')) {
            return 1;
        }
    }
    return 0;
}

int32_t sys_mmap(uint32_t addr, uint32_t len, uint32_t prot, uint32_t flags, uint32_t fd) {
    if (len == 0) return -EINVAL;
    
    process_t* cur = get_current_process();
    if (!cur || !cur->memory) return -ENOMEM;
    
    len = (len + PAGE_SIZE_CONST - 1) & PAGE_MASK;
    
    int is_vga_map = ((flags & MAP_FIXED) && addr == VGA_FRAMEBUFFER_ADDR) || 
                     ((int)fd >= 0 && is_fb_device((int)fd));
    if (is_vga_map) {
        uint32_t fb_flags = PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        for (uint32_t off = 0; off < len && off < VGA_FRAMEBUFFER_SIZE; off += PAGE_SIZE_CONST) {
            map_page_ext(cur->memory->page_directory, 
                        VGA_FRAMEBUFFER_ADDR + off, 
                        VGA_FRAMEBUFFER_ADDR + off, fb_flags);
        }
        return (int32_t)VGA_FRAMEBUFFER_ADDR;
    }
    
    mmap_region_t* regions = cur->memory->mmap_regions;
    
    int slot = -1;
    for (int i = 0; i < MAX_MMAP_REGIONS; i++) {
        if (!regions[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -ENOMEM;
    
    uint32_t map_addr;
    if ((flags & MAP_FIXED) && addr != 0) {
        map_addr = addr & PAGE_MASK;
    } else {
        if (cur->memory->mmap_next_addr == 0) {
            cur->memory->mmap_next_addr = MMAP_BASE;
        }
        map_addr = cur->memory->mmap_next_addr;
        cur->memory->mmap_next_addr += len;
    }
    
    for (uint32_t offset = 0; offset < len; offset += PAGE_SIZE_CONST) {
        uint32_t frame = allocate_frame();
        if (frame == ALLOC_FRAME_FAILED) {
            for (uint32_t rb = 0; rb < offset; rb += PAGE_SIZE_CONST) {
                unmap_page_ext(cur->memory->page_directory, map_addr + rb);
            }
            return -ENOMEM;
        }
        
        /* Security: Always zero frames to prevent information leak */
        /* Previously only zeroed for MAP_ANONYMOUS, but file mappings also need zeroing */
        /* before copying file data to avoid leaking residual data */
        zero_frame(frame);
        
        uint32_t page_flags = PAGE_PRESENT | PAGE_USER;
        if (prot & PROT_WRITE) page_flags |= PAGE_WRITABLE;
        
        if (map_page_ext(cur->memory->page_directory, map_addr + offset, frame, page_flags) < 0) {
            free_frame(frame);
            for (uint32_t rb = 0; rb < offset; rb += PAGE_SIZE_CONST) {
                unmap_page_ext(cur->memory->page_directory, map_addr + rb);
            }
            return -ENOMEM;
        }
    }
    
    regions[slot].addr   = map_addr;
    regions[slot].len    = len;
    regions[slot].prot   = prot;
    regions[slot].flags  = flags;
    regions[slot].in_use = 1;
    
    return (int32_t)map_addr;
}

int32_t sys_munmap(uint32_t addr, uint32_t len, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    if (addr == 0 || len == 0) return -EINVAL;
    
    process_t* cur = get_current_process();
    if (!cur || !cur->memory) return -ESRCH;
    
    len = (len + PAGE_SIZE_CONST - 1) & PAGE_MASK;
    
    mmap_region_t* regions = cur->memory->mmap_regions;
    for (int i = 0; i < MAX_MMAP_REGIONS; i++) {
        if (regions[i].in_use && regions[i].addr == addr) {
            uint32_t flags = local_irq_save();
            for (uint32_t offset = 0; offset < regions[i].len; offset += PAGE_SIZE_CONST) {
                uint32_t phys = get_physical_addr(cur->memory->page_directory, addr + offset);
                if (phys) {
                    unmap_page_ext(cur->memory->page_directory, addr + offset);
                    free_frame(phys);
                }
            }
            regions[i].in_use = 0;
            local_irq_restore(flags);
            return 0;
        }
    }
    return -EINVAL;
}

int32_t sys_mprotect(uint32_t addr, uint32_t len, uint32_t prot, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    if (addr == 0 || len == 0) return -EINVAL;
    if (addr & ~PAGE_MASK) return -EINVAL;  
    
    process_t* cur = get_current_process();
    if (!cur || !cur->memory) return -ESRCH;
    
    len = (len + PAGE_SIZE_CONST - 1) & PAGE_MASK;
    
    for (uint32_t offset = 0; offset < len; offset += PAGE_SIZE_CONST) {
        uint32_t phys = get_physical_addr(cur->memory->page_directory, addr + offset);
        if (phys) {
            uint32_t flags = PAGE_PRESENT | PAGE_USER;
            if (prot & PROT_WRITE) flags |= PAGE_WRITABLE;
            map_page_ext(cur->memory->page_directory, addr + offset, phys, flags);
        }
    }
    
    return 0;
}
