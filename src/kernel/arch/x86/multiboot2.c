/*
 * multiboot2.c — walk the GRUB Multiboot2 tag list passed to entry.asm
 * before kmain, today a no-op walk to MB2_TAG_END that stabilises the
 * entry ABI for future module / memory-map consumers.
 *
 * Invariants:
 *  - Reads only the buffer pointed to by info_addr; returns immediately
 *    if magic != MULTIBOOT2_BOOTLOADER_MAGIC or info_addr is NULL.
 *  - Modifies no kernel state (no globals, no allocator calls); safe to
 *    invoke before paging / acpi / lapic are online.
 *  - struct mb2_tag stays file-private; <kernel/multiboot2.h> exposes
 *    only the prototype and the magic / tag-END constants (V08).
 *
 * Not allowed:
 *  - Call kprintf or any subsystem (mm, vfs, scheduler) — entry.asm runs
 *    this before they exist.
 *  - Dereference tag payloads beyond the size advertised by each tag,
 *    or walk past MB2_TAG_END.
 */

#include <kernel/multiboot2.h>
#include <stdint.h>

struct mb2_tag {
  uint32_t type;
  uint32_t size;
} __attribute__((packed));

void multiboot_parse(uint32_t magic, uint32_t info_addr) {
  if (magic != MULTIBOOT2_BOOTLOADER_MAGIC || !info_addr)
    return;

  struct mb2_tag *tag = (struct mb2_tag *)(info_addr + 8);
  while (tag->type != MB2_TAG_END) {
    uint32_t tag_size = (tag->size + 7) & ~7;
    tag = (struct mb2_tag *)((uint8_t *)tag + tag_size);
  }
}
