/*
 * multiboot.c - Multiboot1 & Multiboot2 info parser
 * 
 * Parses multiboot boot information to extract:
 * - Memory map
 * - Loaded modules (initramfs)
 * 
 * Supports both Multiboot1 (QEMU) and Multiboot2 (GRUB2)
 */

#include <stdint.h>
#include <kernel/kernel.h>

/* Multiboot1 magic number (passed by bootloader in EAX) */
#define MULTIBOOT1_BOOTLOADER_MAGIC 0x2BADB002

/* Multiboot2 magic number */
#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36d76289

/* Multiboot2 tag types */
#define MB2_TAG_END         0
#define MB2_TAG_CMDLINE     1
#define MB2_TAG_BOOTLOADER  2
#define MB2_TAG_MODULE      3
#define MB2_TAG_BASIC_MEM   4
#define MB2_TAG_BOOTDEV     5
#define MB2_TAG_MMAP        6
#define MB2_TAG_FRAMEBUFFER 8

/* Multiboot2 structures */
struct mb2_tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

struct mb2_tag_module {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char cmdline[];
} __attribute__((packed));

struct mb2_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
} __attribute__((packed));

struct mb2_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

/* External initramfs variables (defined in initramfs.c) */
extern uint32_t initramfs_external_addr;
extern uint32_t initramfs_external_size;

/* Multiboot1 info structure */
struct mb1_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    /* ... more fields we don't need ... */
} __attribute__((packed));

struct mb1_module {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t reserved;
} __attribute__((packed));

/*
 * Parse multiboot1 boot information (QEMU -kernel)
 */
static void parse_multiboot1(uint32_t info_addr) {
    struct mb1_info *info = (struct mb1_info*)info_addr;
    
    /* Check if modules are present (bit 3 of flags) */
    if ((info->flags & (1 << 3)) && info->mods_count > 0) {
        struct mb1_module *mods = (struct mb1_module*)info->mods_addr;
        
        /* First module is initramfs */
        initramfs_external_addr = mods[0].mod_start;
        initramfs_external_size = mods[0].mod_end - mods[0].mod_start;
    }
}

/*
 * Parse multiboot2 boot information (GRUB2)
 */
static void parse_multiboot2(uint32_t info_addr) {
    /* Multiboot2 info structure starts with total size */
    uint32_t total_size = *(uint32_t*)info_addr;
    (void)total_size;
    
    /* Tags start at info_addr + 8 */
    struct mb2_tag *tag = (struct mb2_tag*)(info_addr + 8);
    
    while (tag->type != MB2_TAG_END) {
        if (tag->type == MB2_TAG_MODULE) {
            struct mb2_tag_module *mod = (struct mb2_tag_module*)tag;
            if (initramfs_external_addr == 0) {
                initramfs_external_addr = mod->mod_start;
                initramfs_external_size = mod->mod_end - mod->mod_start;
            }
        }
        uint32_t tag_size = (tag->size + 7) & ~7;
        tag = (struct mb2_tag*)((uint8_t*)tag + tag_size);
    }
}

/*
 * Parse multiboot boot information (handles both MB1 and MB2)
 * Called early in boot before kmain
 */
void multiboot_parse(uint32_t magic, uint32_t info_addr) {
    if (!info_addr) return;
    
    if (magic == MULTIBOOT1_BOOTLOADER_MAGIC) {
        parse_multiboot1(info_addr);
    } else if (magic == MULTIBOOT2_BOOTLOADER_MAGIC) {
        parse_multiboot2(info_addr);
    }
    /* Otherwise: legacy boot, no multiboot info */
}
