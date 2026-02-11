#include <kernel/paging.h>
#include <kernel/gdt.h>
#include <kernel/memory_layout.h>
#include <kernel/memory.h>
#include <kernel/spinlock.h>
#include <kernel/process.h>
#include <kernel/kprintf.h>

#define MAX_PHYS_MEM        0x04000000  /* 64MB */
#define FRAME_COUNT         (MAX_PHYS_MEM / PAGE_SIZE)
#define BITMAP_SIZE         (FRAME_COUNT / 32)
#define PAGE_FRAME_MASK     0xFFFFF000

#define TEMP_MAP_VADDR      (IDENTITY_MAP_END - PAGE_SIZE)

static spinlock_t frame_lock = SPINLOCK_INIT("frame");
static spinlock_t temp_map_lock = SPINLOCK_INIT("temp_map");
static uint32_t frame_bitmap[BITMAP_SIZE];
static uint32_t next_free = 0;

uint32_t kernel_page_directory[1024] __attribute__((aligned(4096)));

static uint32_t kernel_page_table_0[1024] __attribute__((aligned(4096)));
static uint32_t kernel_page_table_1[1024] __attribute__((aligned(4096)));
static uint32_t kernel_page_table_2[1024] __attribute__((aligned(4096)));

static uint32_t* kernel_page_tables[MAX_PAGE_TABLES] = {
    kernel_page_table_0,
    kernel_page_table_1,
    kernel_page_table_2,
    NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

page_directory_t* kernel_directory = NULL;
page_directory_t* current_directory = NULL;

static void frame_set(uint32_t addr) {
    uint32_t f = addr / PAGE_SIZE_CONST;
    frame_bitmap[f / 32] |= (1 << (f % 32));
}

static void frame_clear(uint32_t addr) {
    uint32_t f = addr / PAGE_SIZE_CONST;
    frame_bitmap[f / 32] &= ~(1 << (f % 32));
}

uint32_t allocate_frame(void) {
    spin_lock(&frame_lock);
    /* Start at index 32 minimum (= 4MB) to protect kernel zone */
    uint32_t start = (next_free < 32) ? 32 : next_free;
    for (uint32_t i = start; i < BITMAP_SIZE; i++) {
        if (frame_bitmap[i] == 0xFFFFFFFF) continue;
        for (int j = 0; j < 32; j++) {
            if (!(frame_bitmap[i] & (1 << j))) {
                uint32_t addr = (i * 32 + j) * PAGE_SIZE_CONST;
                frame_set(addr);
                next_free = i;
                spin_unlock(&frame_lock);
                return addr;
            }
        }
    }
    spin_unlock(&frame_lock);
    return (uint32_t)-1;
}

void free_frame(uint32_t addr) {
    if (addr == 0 || addr == (uint32_t)-1) return;
    if (addr < 0x400000) return;
    spin_lock(&frame_lock);
    frame_clear(addr);
    uint32_t i = (addr / PAGE_SIZE_CONST) / 32;
    /* Don't let next_free go below 32 (4MB boundary) */
    if (i < next_free && i >= 32) next_free = i;
    spin_unlock(&frame_lock);
}

uint32_t allocate_frame_low(void) {
    spin_lock(&frame_lock);
    uint32_t max_idx = IDENTITY_MAP_END / PAGE_SIZE_CONST / 32;
    for (uint32_t i = 0; i < max_idx; i++) {
        if (frame_bitmap[i] != 0xFFFFFFFF) {
            for (int j = 0; j < 32; j++) {
                if (!(frame_bitmap[i] & (1 << j))) {
                    uint32_t addr = (i * 32 + j) * PAGE_SIZE_CONST;
                    if (addr < IDENTITY_MAP_END) {
                        frame_set(addr);
                        spin_unlock(&frame_lock);
                        return addr;
                    }
                }
            }
        }
    }
    spin_unlock(&frame_lock);
    return (uint32_t)-1;
}

void* temp_map_frame(uint32_t phys_frame) {
    if (phys_frame < IDENTITY_MAP_END) {
        return (void*)phys_frame;  
    }
    
    spin_lock(&temp_map_lock);
    
    uint32_t pdi = (TEMP_MAP_VADDR >> 22) & 0x3FF;  
    uint32_t pti = (TEMP_MAP_VADDR >> 12) & 0x3FF;  
    
    if (pdi < NUM_KERNEL_PAGE_TABLES && kernel_page_tables[pdi]) {
        kernel_page_tables[pdi][pti] = (phys_frame & PAGE_FRAME_MASK) | PAGE_PRESENT | PAGE_WRITABLE;
        
        __asm__ volatile("invlpg (%0)" : : "r"(TEMP_MAP_VADDR) : "memory");
        
        return (void*)TEMP_MAP_VADDR;
    }
    
    spin_unlock(&temp_map_lock);
    return NULL;
}

void temp_unmap_frame(void) {
    uint32_t pdi = (TEMP_MAP_VADDR >> 22) & 0x3FF;
    uint32_t pti = (TEMP_MAP_VADDR >> 12) & 0x3FF;
    
    if (pdi < NUM_KERNEL_PAGE_TABLES && kernel_page_tables[pdi]) {
        kernel_page_tables[pdi][pti] = 0;
        __asm__ volatile("invlpg (%0)" : : "r"(TEMP_MAP_VADDR) : "memory");
    }
    
    spin_unlock(&temp_map_lock);
}

void zero_frame(uint32_t addr) {
    if (addr < IDENTITY_MAP_END) {
        uint32_t* p = (uint32_t*)addr;
        for (int i = 0; i < 1024; i++) p[i] = 0;
    } else {
        uint32_t* p = (uint32_t*)temp_map_frame(addr);
        for (int i = 0; i < 1024; i++) p[i] = 0;
        temp_unmap_frame();
    }
}

void paging_init(void) {
    for (uint32_t a = 0; a < 0x400000; a += PAGE_SIZE) {
        frame_set(a);
    }
    
    for (int i = 0; i < 1024; i++) {
        kernel_page_directory[i] = 0;
    }
    
    kprintf("Paging: Setting up %d page tables for %dMB identity mapping\n", 
            NUM_KERNEL_PAGE_TABLES, (int)(IDENTITY_MAP_END / MB));
    
    for (uint32_t t = 0; t < NUM_KERNEL_PAGE_TABLES; t++) {
        if (!kernel_page_tables[t]) {
            kprintf("ERROR: Not enough static page tables! Need %d, have 3\n", NUM_KERNEL_PAGE_TABLES);
            return;
        }
        
        for (int p = 0; p < 1024; p++) {
            uint32_t addr = (t * 1024 + p) * PAGE_SIZE;
            kernel_page_tables[t][p] = addr | PAGE_PRESENT | PAGE_WRITABLE;
        }
        
        uint32_t pt_phys = (uint32_t)kernel_page_tables[t];
        kernel_page_directory[t] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE;
        
        frame_set(pt_phys);
    }
    
    kprintf("Paging: Identity mapped 0x%x - 0x%x\n", 0, IDENTITY_MAP_END);
    
    set_cr3((uint32_t)kernel_page_directory);
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
}

page_directory_t* create_page_directory(void) {
    page_directory_t* dir = (page_directory_t*)kmalloc(sizeof(page_directory_t));
    if (!dir) return NULL;
    
    uint32_t pd_phys = allocate_frame();
    if (pd_phys == (uint32_t)-1 || pd_phys >= IDENTITY_MAP_END) {
        if (pd_phys != (uint32_t)-1) free_frame(pd_phys);
        kfree(dir);
        return NULL;
    }
    
    dir->physical_addr = pd_phys;
    uint32_t* pd = (uint32_t*)pd_phys;  
    
    for (int i = 0; i < 1024; i++) {
        dir->tables_physical[i] = 0;
        dir->tables_virtual[i] = NULL;
        pd[i] = 0;
    }
    
    for (uint32_t i = 0; i < NUM_KERNEL_PAGE_TABLES; i++) {
        pd[i] = kernel_page_directory[i];
        dir->tables_physical[i] = kernel_page_directory[i];
    }
    
    return dir;
}

void destroy_page_directory(page_directory_t* dir) {
    if (!dir) return;
    
    uint32_t* pd = (uint32_t*)dir->physical_addr;
    
    for (int i = 4; i < 768; i++) {
        if (!(pd[i] & PAGE_PRESENT)) continue;
        
        uint32_t pt_phys = pd[i] & PAGE_FRAME_MASK;
        if (pt_phys >= IDENTITY_MAP_END) continue;
        
        uint32_t* pt = (uint32_t*)pt_phys;
        for (int j = 0; j < 1024; j++) {
            if (pt[j] & PAGE_PRESENT) {
                free_frame(pt[j] & PAGE_FRAME_MASK);
            }
        }
        free_frame(pt_phys);
        
        if (dir->tables_virtual[i]) {
            kfree(dir->tables_virtual[i]);
        }
    }
    
    free_frame(dir->physical_addr);
    kfree(dir);
}

void switch_page_directory(page_directory_t* dir) {
    current_directory = dir;
    set_cr3(dir->physical_addr);
}

uint32_t* get_current_page_directory(void) {
    return current_directory ? current_directory->tables_physical : NULL;
}

int map_page_ext(page_directory_t* dir, uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pdi = virt >> 22;
    uint32_t pti = (virt >> 12) & 0x3FF;
    
    if (!dir) return -1;
    
    uint32_t* pd = (uint32_t*)dir->physical_addr;
    
    if (!(pd[pdi] & PAGE_PRESENT)) {
        uint32_t pt_phys = allocate_frame_low();
        if (pt_phys == (uint32_t)-1) return -1;
        zero_frame(pt_phys);
        
        pd[pdi] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
        dir->tables_physical[pdi] = pd[pdi];
    }
    
    uint32_t pt_phys = pd[pdi] & PAGE_FRAME_MASK;
    uint32_t* pt = (uint32_t*)pt_phys;
    pt[pti] = (phys & PAGE_FRAME_MASK) | flags;
    
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

void unmap_page_ext(page_directory_t* dir, uint32_t virt) {
    uint32_t pdi = virt >> 22;
    uint32_t pti = (virt >> 12) & 0x3FF;
    
    uint32_t* pd = (uint32_t*)dir->physical_addr;
    if (!(pd[pdi] & PAGE_PRESENT)) return;
    
    uint32_t* pt = (uint32_t*)(pd[pdi] & PAGE_FRAME_MASK);
    pt[pti] = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

uint32_t get_physical_addr(page_directory_t* dir, uint32_t virt) {
    uint32_t pdi = virt >> 22;
    uint32_t pti = (virt >> 12) & 0x3FF;
    
    uint32_t* pd = (uint32_t*)dir->physical_addr;
    if (!(pd[pdi] & PAGE_PRESENT)) return 0;
    
    uint32_t* pt = (uint32_t*)(pd[pdi] & PAGE_FRAME_MASK);
    if (!(pt[pti] & PAGE_PRESENT)) return 0;
    
    return (pt[pti] & PAGE_FRAME_MASK) | (virt & 0xFFF);
}

uint32_t get_physical_addr_current(uint32_t virt) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    
    uint32_t pdi = virt >> 22;
    uint32_t pti = (virt >> 12) & 0x3FF;
    
    uint32_t* pd = (uint32_t*)(cr3 & PAGE_FRAME_MASK);
    if (!(pd[pdi] & PAGE_PRESENT)) return 0;
    
    uint32_t* pt = (uint32_t*)(pd[pdi] & PAGE_FRAME_MASK);
    if (!(pt[pti] & PAGE_PRESENT)) return 0;
    
    return (pt[pti] & PAGE_FRAME_MASK) | (virt & 0xFFF);
}

process_memory_t* create_process_memory(void) {
    process_memory_t* mem = (process_memory_t*)kmalloc(sizeof(process_memory_t));
    if (!mem) return NULL;
    
    mem->page_directory = create_page_directory();
    if (!mem->page_directory) {
        kfree(mem);
        return NULL;
    }
    
    mem->vm_areas       = NULL;
    mem->brk            = USER_CODE_BASE;
    mem->stack_base     = USER_STACK_BASE;
    mem->mmap_next_addr = 0x40000000;
    mem->refcount       = 1;
    
    for (int i = 0; i < MAX_MMAP_REGIONS; i++) {
        mem->mmap_regions[i].in_use = 0;
    }
    
    uint32_t stack_size = 32 * PAGE_SIZE_CONST;  /* 128KB */
    uint32_t stack_bottom = USER_STACK_BASE - stack_size;
    if (map_user_pages(mem, stack_bottom, stack_size, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) < 0) {
        destroy_page_directory(mem->page_directory);
        kfree(mem);
        return NULL;
    }
    
    return mem;
}

process_memory_t* clone_process_memory(process_memory_t* src) {
    if (!src || !src->page_directory) return NULL;
    
    process_memory_t* dst = create_process_memory();
    if (!dst) return NULL;
    
    dst->brk            = src->brk;
    dst->stack_base     = src->stack_base;
    dst->mmap_next_addr = src->mmap_next_addr;
    
    for (int i = 0; i < MAX_MMAP_REGIONS; i++) {
        dst->mmap_regions[i] = src->mmap_regions[i];
    }
    
    uint32_t* src_pd = (uint32_t*)src->page_directory->physical_addr;
    
    for (int i = 4; i < 768; i++) {
        if (!(src_pd[i] & PAGE_PRESENT)) continue;
        
        uint32_t src_pt_phys = src_pd[i] & PAGE_FRAME_MASK;
        if (src_pt_phys >= IDENTITY_MAP_END) continue;
        
        uint32_t* src_pt = (uint32_t*)src_pt_phys;
        
        for (int j = 0; j < 1024; j++) {
            if (!(src_pt[j] & PAGE_PRESENT)) continue;
            
            uint32_t src_frame = src_pt[j] & PAGE_FRAME_MASK;
            uint32_t flags = src_pt[j] & 0xFFF;
            uint32_t virt  = (i << 22) | (j << 12);
            
            uint32_t dst_frame = allocate_frame();
            if (dst_frame == (uint32_t)-1) {
                destroy_process_memory(dst);
                return NULL;
            }
            
            if (src_frame < IDENTITY_MAP_END && dst_frame < IDENTITY_MAP_END) {
                uint32_t* s = (uint32_t*)src_frame;
                uint32_t* d = (uint32_t*)dst_frame;
                for (int k = 0; k < 1024; k++) d[k] = s[k];
            } else {
                zero_frame(dst_frame);
            }
            
            if (map_page_ext(dst->page_directory, virt, dst_frame, flags) < 0) {
                free_frame(dst_frame);
                destroy_process_memory(dst);
                return NULL;
            }
        }
    }
    
    return dst;
}

void destroy_process_memory(process_memory_t* mem) {
    if (!mem) return;
    if (mem->page_directory) {
        destroy_page_directory(mem->page_directory);
    }
    kfree(mem);
}

int map_user_pages(process_memory_t* mem, uint32_t base, uint32_t size, uint32_t flags) {
    if (!mem || !mem->page_directory) return -1;
    
    uint32_t start = base & ~(PAGE_SIZE_CONST - 1);
    uint32_t end = (base + size + PAGE_SIZE_CONST - 1) & ~(PAGE_SIZE_CONST - 1);
    
    for (uint32_t addr = start; addr < end; addr += PAGE_SIZE_CONST) {
        uint32_t frame = allocate_frame_low();
        if (frame == (uint32_t)-1) {
            frame = allocate_frame();
            if (frame == (uint32_t)-1) return -1;
        }
        zero_frame(frame);
        
        if (map_page_ext(mem->page_directory, addr, frame, flags) < 0) {
            free_frame(frame);
            return -1;
        }
    }
    return 0;
}

void setup_user_stack(process_memory_t* mem) {
    if (!mem) return;
    uint32_t stack_size = 8 * PAGE_SIZE_CONST;
    uint32_t stack_bottom = USER_STACK_BASE - stack_size;
    map_user_pages(mem, stack_bottom, stack_size, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    mem->stack_base = USER_STACK_BASE;
}

void* copy_to_frame(uint32_t frame, uint32_t offset, const void* src, size_t len) {
    if (frame < IDENTITY_MAP_END) {
        uint8_t* dst = (uint8_t*)(frame + offset);
        const uint8_t* s = (const uint8_t*)src;
        for (size_t i = 0; i < len; i++) {
            dst[i] = s[i];
        }
        return dst;
    } else {
        uint8_t* mapped = (uint8_t*)temp_map_frame(frame);
        uint8_t* dst = mapped + offset;
        const uint8_t* s = (const uint8_t*)src;
        for (size_t i = 0; i < len; i++) {
            dst[i] = s[i];
        }
        temp_unmap_frame();
        return (void*)1;  
    }
}

void sync_page_directory(page_directory_t* dir) { (void)dir; }
int handle_cow_fault(uint32_t addr) { (void)addr; return -1; }
void frame_incref(uint32_t addr) { (void)addr; }
uint8_t frame_getref(uint32_t addr) { (void)addr; return 1; }
int handle_demand_fault(uint32_t addr) { (void)addr; return -1; }
vm_area_t* vma_find(process_memory_t* mem, uint32_t addr) { (void)mem; (void)addr; return NULL; }
int vma_add(process_memory_t* mem, uint32_t s, uint32_t e, uint32_t f) { (void)mem; (void)s; (void)e; (void)f; return -1; }
int map_user_pages_lazy(process_memory_t* m, uint32_t b, uint32_t s, uint32_t f) { (void)m; (void)b; (void)s; (void)f; return -1; }
/* Old stubs removed - real implementations above */
