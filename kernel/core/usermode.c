#include <stdint.h>
#include <kernel/constants.h>
#include <kernel/gdt.h>
#include <kernel/process.h>
#include <kernel/vfs.h>
#include <kernel/paging.h>
#include <kernel/console.h>
#include <kernel/kprintf.h>

extern void jump_to_usermode(uint32_t entry_point, uint32_t user_stack);

#define ELF_MAGIC        0x464C457F

static uint8_t interrupt_stack[8192] __attribute__((aligned(4096)));

static inline uint32_t read_le32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static uint32_t load_init_elf(process_memory_t* mem) {
    if (!mem || !mem->page_directory) return 0;
    
    int fd = vfs_open("/sbin/init", 0, 0);
    if (fd < 0) return 0;
    
    uint8_t hdr[84];
    if (vfs_read(fd, hdr, sizeof(hdr)) < 84 || read_le32(hdr) != ELF_MAGIC) {
        vfs_close(fd);
        return 0;
    }
    
    uint32_t entry = read_le32(hdr + 24);
    if (entry == 0 || entry >= KERNEL_BASE) {
        vfs_close(fd);
        return 0;
    }
    
    uint32_t p_offset = read_le32(hdr + 52 + 4);
    uint32_t p_vaddr  = read_le32(hdr + 52 + 8);
    uint32_t p_filesz = read_le32(hdr + 52 + 16);
    uint32_t p_memsz  = read_le32(hdr + 52 + 20);
    
    kprintf("ELF: entry=0x%x vaddr=0x%x size=%d\n", entry, p_vaddr, p_filesz);
    
    uint32_t vaddr_start = p_vaddr & ~0xFFF;
    uint32_t vaddr_end = (p_vaddr + p_memsz + PAGE_SIZE_CONST + 0xFFF) & ~0xFFF;
    
    for (uint32_t vaddr = vaddr_start; vaddr < vaddr_end; vaddr += PAGE_SIZE_CONST) {
        uint32_t paddr = allocate_frame();
        if (!paddr || paddr == (uint32_t)-1) {
            vfs_close(fd);
            return 0;
        }
        zero_frame(paddr);
        map_page_ext(mem->page_directory, vaddr, paddr, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    }
    
    vfs_lseek(fd, p_offset, 0);
    uint8_t buf[512];
    uint32_t loaded = 0;
    
    while (loaded < p_filesz) {
        uint32_t chunk = (p_filesz - loaded > 512) ? 512 : p_filesz - loaded;
        ssize_t r = vfs_read(fd, buf, chunk);
        if (r <= 0) break;
        
        for (ssize_t i = 0; i < r; i++) {
            uint32_t vaddr = p_vaddr + loaded + i;
            uint32_t paddr = get_physical_addr(mem->page_directory, vaddr);
            if (paddr) {
                uint32_t frame_base = paddr & ~0xFFF;
                uint32_t offset = paddr & 0xFFF;
                uint8_t* page;
                if (frame_base < 0x01000000) {
                    page = (uint8_t*)paddr;
                    page[0] = buf[i];  /* Direct write */
                } else {
                    page = (uint8_t*)temp_map_frame(frame_base);
                    page[offset] = buf[i];
                    temp_unmap_frame();
                }
            }
        }
        loaded += r;
    }
    
    vfs_close(fd);
    kprintf("INIT: loaded %d bytes (expected %d)\n", loaded, p_filesz);
    return entry;
}

void test_userspace(void) {
    extern void tss_set_kernel_stack(uint32_t esp0);
    tss_set_kernel_stack((uint32_t)interrupt_stack + sizeof(interrupt_stack));
    
    console_init();
    
    process_t *init_proc = process_create("init", NULL);
    if (!init_proc) {
        kprintf("ERROR: Failed to create init process\n");
        while(1) __asm__ volatile("hlt");
    }
    init_proc->state = PROCESS_RUNNING;
    process_switch(init_proc);  /* Make init the current process */
    
    console_open_stdio(init_proc);
    kprintf("INIT: fd[0].node_idx=%x (expect C0C0)\n", init_proc->fd_table[0].node_idx);
    
    init_proc->memory = create_process_memory();
    if (!init_proc->memory) {
        kprintf("ERROR: Failed to create memory context\n");
        while(1) __asm__ volatile("hlt");
    }
    
    process_init_canary(init_proc);
    
    uint32_t entry = load_init_elf(init_proc->memory);
    if (!entry) {
        kprintf("ERROR: Failed to load init\n");
        while(1) __asm__ volatile("hlt");
    }
    
    sync_page_directory(init_proc->memory->page_directory);
    
    switch_page_directory(init_proc->memory->page_directory);
    
    tss_set_kernel_stack((uint32_t)init_proc->kernel_stack + KERNEL_STACK_SIZE);
    
    uint32_t stack_ptr = USER_STACK_BASE - 4096;  /* Start within mapped region */
    
    uint32_t str_addr   = stack_ptr - 8;  
    uint32_t argv_addr  = str_addr  - 8;  
    uint32_t frame_addr = argv_addr - 16; 
    
    uint32_t phys = get_physical_addr(init_proc->memory->page_directory, str_addr);
    if (phys) {
        uint32_t frame_base = phys & ~0xFFF;
        uint32_t offset = phys & 0xFFF;
        char *dst;
        if (frame_base < 0x01000000) {
            dst = (char*)phys;
        } else {
            dst = (char*)temp_map_frame(frame_base) + offset;
        }
        dst[0] = 'i'; dst[1] = 'n'; dst[2] = 'i'; dst[3] = 't'; dst[4] = 0;
        if (frame_base >= 0x01000000) temp_unmap_frame();
    }
    
    phys = get_physical_addr(init_proc->memory->page_directory, argv_addr);
    if (phys) {
        uint32_t frame_base = phys & ~0xFFF;
        uint32_t offset = phys & 0xFFF;
        uint32_t *argv;
        if (frame_base < 0x01000000) {
            argv = (uint32_t*)phys;
        } else {
            argv = (uint32_t*)((char*)temp_map_frame(frame_base) + offset);
        }
        argv[0] = str_addr;  /* argv[0] = "init" */
        argv[1] = 0;         /* argv[1] = NULL */
        if (frame_base >= 0x01000000) temp_unmap_frame();
    }
    
    phys = get_physical_addr(init_proc->memory->page_directory, frame_addr);
    if (phys) {
        uint32_t frame_base = phys & ~0xFFF;
        uint32_t offset = phys & 0xFFF;
        uint32_t *frame;
        if (frame_base < 0x01000000) {
            frame = (uint32_t*)phys;
        } else {
            frame = (uint32_t*)((char*)temp_map_frame(frame_base) + offset);
        }
        frame[0] = 0;         /* return address */
        frame[1] = 1;         /* argc = 1 */
        frame[2] = argv_addr; /* argv */
        frame[3] = 0;         /* envp = NULL */
        if (frame_base >= 0x01000000) temp_unmap_frame();
    }
    
    jump_to_usermode(entry, frame_addr);
    
    while(1) __asm__ volatile("hlt");
}
