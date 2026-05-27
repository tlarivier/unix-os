/*
 * elf_loader.c — load an ELF32 i386 image (ET_EXEC or PIE ET_DYN) into a
 * process_t address space, streaming PT_LOAD segments via the VFS and
 * chaining PT_INTERP when present.
 *
 * Invariants:
 *  - Caller owns proc->memory (non-NULL, page_directory valid).
 *  - CR3 must point at kernel_page_directory for the duration of the load;
 *    segment frames are written via temporary kernel mappings.
 *  - All PT_LOAD pages map PAGE_USER|PAGE_PRESENT (+PAGE_WRITABLE iff PF_W),
 *    bounded to [USER_CODE_BASE, USER_SPACE_END).
 *  - generate_pie_base() is sampled at most once per load (ET_DYN only).
 *  - Header validation precedes every dereference of e_phnum/e_entry.
 *
 * Not allowed:
 *  - Calling schedule(), yielding, or holding any lock across vfs_read.
 *  - Applying R_386_* relocations (rtld.c owns dynamic relocation).
 *  - Exposing Elf32_* / EI_* / PT_* macros outside this translation unit.
 */

#include <kernel/constants.h>
#include <kernel/elf.h>
#include <kernel/elf_loader.h>
#include <kernel/errno.h>
#include <kernel/kernel.h>
#include <kernel/memory.h>
#include <kernel/paging.h>
#include <kernel/process.h>
#include <kernel/random.h>
#include <kernel/uaccess.h>
#include <kernel/vfs.h>

#ifndef UINT32_MAX
#define UINT32_MAX 0xFFFFFFFFU
#endif

#define ELF_MAX_PHDR 32U
#define ELF_MAX_SHDR 1024U
#define ELF_INTERP_PATH_MAX 256U
#define ELF_READ_CHUNK 512U
#define ELF_INTERP_RESERVE 0x400000U
#define ELF_PIE_BASE_MIN 0x10000000U
#define ELF_PIE_BASE_RANGE 0x30000000U
#define ELF_PATH_MAX 256U

_Static_assert(ELF_MAX_PHDR * sizeof(Elf32_Phdr) <= 1024,
               "phdr stack array too large; reduce ELF_MAX_PHDR");

static uint32_t generate_pie_base(void) {
  uint32_t rnd = random_u32();
  uint32_t range = ELF_PIE_BASE_RANGE / PAGE_SIZE_CONST;
  uint32_t base = ELF_PIE_BASE_MIN + (rnd % range) * PAGE_SIZE_CONST;
  return base & ~(PAGE_SIZE_CONST - 1U);
}

static int32_t elf_seek_read(int32_t fd, uint32_t off, void *buf,
                             uint32_t len) {
  off_t r = vfs_lseek(fd, (off_t)off, 0);
  if (r < 0 || (uint32_t)r != off)
    return -EIO;
  ssize_t got = vfs_read(fd, buf, len);
  if (got < 0 || (uint32_t)got != len)
    return -EIO;
  return 0;
}

static int32_t validate_elf_header(Elf32_Ehdr *header) {
  if (header->e_ident[EI_MAG0] != ELFMAG0 ||
      header->e_ident[EI_MAG1] != ELFMAG1 ||
      header->e_ident[EI_MAG2] != ELFMAG2 ||
      header->e_ident[EI_MAG3] != ELFMAG3) {
    return -EINVAL;
  }

  if (header->e_ident[EI_CLASS] != ELFCLASS32)
    return -EINVAL;
  if (header->e_ident[EI_DATA] != ELFDATA2LSB)
    return -EINVAL;
  if (header->e_machine != EM_386)
    return -EINVAL;

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

  if (header->e_phentsize != sizeof(Elf32_Phdr))
    return -EINVAL;
  if (header->e_phnum > ELF_MAX_PHDR)
    return -EINVAL;
  if (header->e_shnum > ELF_MAX_SHDR)
    return -EINVAL;

  return is_pie;
}

static int32_t map_segment_streaming(int32_t fd, Elf32_Phdr *phdr,
                                     process_memory_t *mem,
                                     uint32_t base_addr) {
  if (phdr->p_type != PT_LOAD || phdr->p_memsz == 0) {
    return 0;
  }

  uint32_t actual_vaddr = phdr->p_vaddr + base_addr;

  if (actual_vaddr < USER_CODE_BASE || actual_vaddr >= USER_SPACE_END) {
    return -EFAULT;
  }

  uint64_t vaddr_end64 =
      (uint64_t)actual_vaddr + (uint64_t)phdr->p_memsz + (PAGE_SIZE_CONST - 1);
  if (vaddr_end64 > 0xFFFFFFFFULL)
    return -EOVERFLOW;

  uint32_t vaddr_start = actual_vaddr & ~(PAGE_SIZE_CONST - 1);
  uint32_t vaddr_end = ((uint32_t)vaddr_end64) & ~(PAGE_SIZE_CONST - 1);
  uint32_t num_pages = (vaddr_end - vaddr_start) / PAGE_SIZE_CONST;

  uint32_t page_flags = PAGE_PRESENT | PAGE_USER;
  if (phdr->p_flags & PF_W)
    page_flags |= PAGE_WRITABLE;

  uint32_t segment_offset = actual_vaddr - vaddr_start;
  uint32_t file_bytes_remaining = phdr->p_filesz;

  {
    off_t s = vfs_lseek(fd, (off_t)phdr->p_offset, 0);
    if (s < 0 || (uint32_t)s != phdr->p_offset)
      return -EIO;
  }

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
        uint8_t buf[ELF_READ_CHUNK];
        uint32_t bytes_read_total = 0;

        while (bytes_read_total < bytes_to_read) {
          uint32_t chunk = bytes_to_read - bytes_read_total;
          if (chunk > ELF_READ_CHUNK)
            chunk = ELF_READ_CHUNK;

          ssize_t r = vfs_read(fd, buf, chunk);
          if (r <= 0)
            break;

          copy_to_frame(physical_addr, page_start + bytes_read_total, buf, r);
          bytes_read_total += r;
        }

        file_bytes_remaining -= bytes_read_total;
      }
    }

    if (map_page(mem->page_directory, virtual_addr, physical_addr, page_flags) <
        0) {
      free_frame(physical_addr);
      return -EIO;
    }
  }

  return 0;
}

static int32_t open_and_read_phdrs(const char *path, Elf32_Ehdr *out_ehdr,
                                   Elf32_Phdr *out_phdrs, int32_t *out_fd,
                                   int *out_is_pie) {
  int32_t fd = vfs_open(path, 0, 0644);
  if (fd < 0)
    return -ENOENT;

  if (vfs_read(fd, out_ehdr, sizeof(*out_ehdr)) != (ssize_t)sizeof(*out_ehdr)) {
    vfs_close(fd);
    return -EIO;
  }

  int32_t v = validate_elf_header(out_ehdr);
  if (IS_ERROR(v)) {
    vfs_close(fd);
    return v;
  }

  uint32_t phdrs_bytes =
      (uint32_t)out_ehdr->e_phnum * (uint32_t)sizeof(Elf32_Phdr);
  int32_t rc = elf_seek_read(fd, out_ehdr->e_phoff, out_phdrs, phdrs_bytes);
  if (IS_ERROR(rc)) {
    vfs_close(fd);
    return rc;
  }

  *out_fd = fd;
  *out_is_pie = v;
  return 0;
}

static int32_t elf_load_segments(int32_t fd, Elf32_Phdr *phdrs, int n,
                                 process_memory_t *mem, uint32_t base_addr,
                                 uint32_t *out_brk) {
  uint32_t brk_end = 0;
  for (int i = 0; i < n; i++) {
    int32_t result = map_segment_streaming(fd, &phdrs[i], mem, base_addr);
    if (IS_ERROR(result))
      return result;

    if (phdrs[i].p_type == PT_LOAD) {
      uint64_t seg_end64 = (uint64_t)phdrs[i].p_vaddr + (uint64_t)base_addr +
                           (uint64_t)phdrs[i].p_memsz + (PAGE_SIZE_CONST - 1);
      if (seg_end64 <= 0xFFFFFFFFULL) {
        uint32_t seg_end = ((uint32_t)seg_end64) & ~(PAGE_SIZE_CONST - 1);
        if (seg_end > brk_end)
          brk_end = seg_end;
      }
    }
  }
  *out_brk = brk_end;
  return 0;
}

static const char *elf_find_interp(int32_t fd, Elf32_Phdr *phdrs, int n,
                                   char buf[ELF_INTERP_PATH_MAX]) {
  for (int i = 0; i < n; i++) {
    if (phdrs[i].p_type == PT_INTERP && phdrs[i].p_filesz > 0 &&
        phdrs[i].p_filesz < ELF_INTERP_PATH_MAX) {
      if (IS_ERROR(
              elf_seek_read(fd, phdrs[i].p_offset, buf, phdrs[i].p_filesz))) {
        return NULL;
      }
      buf[phdrs[i].p_filesz] = '\0';
      return buf;
    }
  }
  return NULL;
}

static void elf_setup_context(process_t *proc, uint32_t entry) {
  proc->context.eip = entry;
  proc->context.cs = USER_CODE_SEL;
  proc->context.ds = USER_DATA_SEL;
  proc->context.es = USER_DATA_SEL;
  proc->context.fs = USER_DATA_SEL;
  proc->context.gs = USER_DATA_SEL;
  proc->context.ss = USER_DATA_SEL;
  proc->context.esp = proc->user_stack_base - 4;
}

static int32_t load_interpreter_at(const char *path, process_t *proc,
                                   uint32_t base_addr) {
  Elf32_Ehdr ehdr;
  Elf32_Phdr phdrs[ELF_MAX_PHDR];
  int32_t fd = -1;
  int is_pie_unused = 0;

  int32_t rc = open_and_read_phdrs(path, &ehdr, phdrs, &fd, &is_pie_unused);
  if (IS_ERROR(rc))
    return rc;

  uint32_t brk_unused = 0;
  rc = elf_load_segments(fd, phdrs, ehdr.e_phnum, proc->memory, base_addr,
                         &brk_unused);
  vfs_close(fd);
  if (IS_ERROR(rc))
    return rc;

  return (int32_t)(ehdr.e_entry + base_addr);
}

int32_t load_elf_process_into(const char *path, process_t *proc,
                              process_memory_t *mem_override) {
  if (!proc || !mem_override)
    return -EINVAL;
  process_memory_t *saved = proc->memory;
  proc->memory = mem_override;
  int32_t rc = load_elf_process(path, proc);
  proc->memory = saved;
  return rc;
}

int32_t load_elf_process(const char *path, process_t *proc) {
  if (IS_ERROR(validate_kernel_string(path, ELF_PATH_MAX)))
    return -EINVAL;

  Elf32_Ehdr elf_header;
  Elf32_Phdr phdrs[ELF_MAX_PHDR];
  int32_t fd = -1;
  int is_pie = 0;

  int32_t rc = open_and_read_phdrs(path, &elf_header, phdrs, &fd, &is_pie);
  if (IS_ERROR(rc))
    return rc;

  uint32_t base_addr = is_pie ? generate_pie_base() : 0;

  uint32_t brk_end = 0;
  rc = elf_load_segments(fd, phdrs, elf_header.e_phnum, proc->memory, base_addr,
                         &brk_end);
  if (IS_ERROR(rc)) {
    vfs_close(fd);
    return rc;
  }

  if (brk_end > 0)
    proc->memory->brk = brk_end;

  char interp_buf[ELF_INTERP_PATH_MAX];
  const char *interp_path =
      elf_find_interp(fd, phdrs, elf_header.e_phnum, interp_buf);

  if (interp_path &&
      IS_ERROR(validate_kernel_string(interp_path, ELF_INTERP_PATH_MAX))) {
    vfs_close(fd);
    return -EINVAL;
  }

  vfs_close(fd);

  uint32_t entry_point = elf_header.e_entry + base_addr;

  if (interp_path) {
    uint32_t interp_base = proc->memory->mmap_next_addr;
    uint64_t next64 = (uint64_t)interp_base + (uint64_t)ELF_INTERP_RESERVE;
    if (next64 > (uint64_t)USER_SPACE_END)
      return -ENOMEM;
    proc->memory->mmap_next_addr = (uint32_t)next64;

    int32_t interp_entry = load_interpreter_at(interp_path, proc, interp_base);
    if (IS_ERROR(interp_entry))
      return interp_entry;

    entry_point = (uint32_t)interp_entry;

    uint32_t load_base = 0;
    for (int i = 0; i < elf_header.e_phnum; i++) {
      if (phdrs[i].p_type == PT_LOAD && phdrs[i].p_offset == 0) {
        load_base = phdrs[i].p_vaddr;
        break;
      }
    }

    proc->elf_entry = elf_header.e_entry + base_addr;
    proc->elf_phdr = load_base + base_addr + elf_header.e_phoff;
    proc->elf_phnum = elf_header.e_phnum;
    proc->interp_base = interp_base;
  }

  elf_setup_context(proc, entry_point);
  return 0;
}
