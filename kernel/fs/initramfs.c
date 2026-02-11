#include <kernel/vfs.h>
#include <kernel/memory.h>
#include <kernel/kprintf.h>
#include <kernel/errno.h>
#include <stdint.h>

#ifdef HAVE_INITRAMFS
#include "initramfs_data.h"
#endif

typedef struct {
    char c_magic[6];      /* "070701" or "070702" */
    char c_ino[8];
    char c_mode[8];
    char c_uid[8];
    char c_gid[8];
    char c_nlink[8];
    char c_mtime[8];
    char c_filesize[8];
    char c_devmajor[8];
    char c_devminor[8];
    char c_rdevmajor[8];
    char c_rdevminor[8];
    char c_namesize[8];
    char c_check[8];
} cpio_newc_header_t;

#define CPIO_MAGIC "070701"
#define CPIO_TRAILER "TRAILER!!!"

#ifndef O_CREAT
#define O_CREAT  0x0100
#endif
#ifndef O_WRONLY
#define O_WRONLY 1
#endif

static uint32_t parse_hex8(const char *s) {
    uint32_t val = 0;
    for (int i = 0; i < 8; i++) {
        char c = s[i];
        val <<= 4;
        if (c >= '0' && c <= '9') val |= (c - '0');
        else if (c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
    }
    return val;
}

static inline uint32_t align4(uint32_t val) {
    return (val + 3) & ~3;
}

static int cpio_strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

static int cpio_strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && *s1 == *s2) { s1++; s2++; n--; }
    return n ? (unsigned char)*s1 - (unsigned char)*s2 : 0;
}

static void create_parent_dirs(const char *path) {
    char dir[256];
    int i = 0;
    
    while (path[i] && i < 255) {
        dir[i] = path[i];
        if (path[i] == '/' && i > 0) {
            dir[i] = '\0';
            vfs_mkdir(dir, 0755);
            dir[i] = '/';
        }
        i++;
    }
}

int initramfs_load(const void *data, size_t size) {
    const uint8_t *ptr = (const uint8_t *)data;
    const uint8_t *end = ptr + size;
    int file_count = 0;
    
    kprintf("Loading initramfs (%d bytes)...\n", (int)size);
    
    while (ptr + sizeof(cpio_newc_header_t) < end) {
        const cpio_newc_header_t *hdr = (const cpio_newc_header_t *)ptr;
        
        if (cpio_strncmp(hdr->c_magic, CPIO_MAGIC, 6) != 0) {
            kprintf("initramfs: invalid CPIO magic at offset %d\n", (int)(ptr - (const uint8_t*)data));
            break;
        }
        
        uint32_t mode     = parse_hex8(hdr->c_mode);
        uint32_t filesize = parse_hex8(hdr->c_filesize);
        uint32_t namesize = parse_hex8(hdr->c_namesize);
        
        const char *name = (const char *)(ptr + sizeof(cpio_newc_header_t));
        uint32_t name_padded = align4(sizeof(cpio_newc_header_t) + namesize);
        
        const uint8_t *file_data = ptr + name_padded;
        uint32_t data_padded = align4(filesize);
        
        if (cpio_strcmp(name, CPIO_TRAILER) == 0) {
            kprintf("initramfs: found TRAILER, stopping\n");
            break;
        }
        
        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '/' && name[2] == '\0'))) {
            ptr += name_padded + data_padded;
            continue;
        }
        
        char path[256];
        if (name[0] == '.') {
            /* "./foo" -> "/foo" */
            int i = 0, j = 1;
            path[i++] = '/';
            while (name[j] && i < 255) {
                if (name[j] == '/' && i == 1) { j++; continue; }
                path[i++] = name[j++];
            }
            path[i] = '\0';
        } else if (name[0] != '/') {
            /* "foo" -> "/foo" */
            path[0] = '/';
            int i = 1;
            while (name[i-1] && i < 255) {
                path[i] = name[i-1];
                i++;
            }
            path[i] = '\0';
        } else {
            /* Already absolute */
            int i = 0;
            while (name[i] && i < 255) { path[i] = name[i]; i++; }
            path[i] = '\0';
        }
        
        if ((mode & 0170000) == 0040000) {
            create_parent_dirs(path);
            vfs_mkdir(path, mode & 0777);
        } else if ((mode & 0170000) == 0100000) {
            create_parent_dirs(path);
            
            int fd = vfs_open(path, O_CREAT | O_WRONLY, mode & 0777);
            if (fd >= 0) {
                if (filesize > 0) {
                    ssize_t written = vfs_write(fd, file_data, filesize);
                    if (written != (ssize_t)filesize) {
                        kprintf("initramfs: write error for %s: wrote %d/%d\n", path, (int)written, (int)filesize);
                    }
                }
                vfs_close(fd);
                file_count++;
            } else {
                kprintf("initramfs: failed to open %s: %d\n", path, fd);
            }
        }
        
        ptr += name_padded + data_padded;
    }
    
    kprintf("Loaded %d files from initramfs\n", file_count);
    return file_count;
}

int initramfs_load_external(uint32_t addr, uint32_t size) {
    if (!addr || size < 110) {  
        return -EINVAL;
    }
    kprintf("Loading external initramfs from 0x%x (%d bytes)\n", addr, size);
    return initramfs_load((const unsigned char*)addr, size);
}

int initramfs_init(void) {
#ifdef HAVE_INITRAMFS
    kprintf("Loading from embedded initramfs...\n");
    return initramfs_load(initramfs_data, initramfs_size);
#else
    extern uint32_t initramfs_external_addr;
    extern uint32_t initramfs_external_size;
    
    if (initramfs_external_addr && initramfs_external_size) {
        int result = initramfs_load_external(initramfs_external_addr, 
                                              initramfs_external_size);
        if (result > 0) return result;
    }
    
    return 0;
#endif
}

uint32_t initramfs_external_addr = 0;
uint32_t initramfs_external_size = 0;
