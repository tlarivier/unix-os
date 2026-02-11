#include <kernel/vfs.h>
#include <kernel/kprintf.h>
#include <kernel/initramfs.h>
#include <stdint.h>

#ifndef O_CREAT
#define O_CREAT  0x0100
#endif
#ifndef O_WRONLY
#define O_WRONLY 1
#endif

#ifdef USE_EMBEDDED_BINS
#include "userspace_bins.h"
#include "libs_data.h"

static int install_binary(const char* path, const uint8_t* data, size_t size) {
    int fd = vfs_open(path, O_CREAT | O_WRONLY, 0755);
    if (fd < 0) return fd;
    ssize_t n = vfs_write(fd, data, size);
    vfs_close(fd);
    return (n == (ssize_t)size) ? 0 : -1;
}
#endif

int install_userspace_binaries(void) {
    vfs_mkdir("/bin",  0755);
    vfs_mkdir("/sbin", 0755);
    vfs_mkdir("/lib",  0755);
    
#ifdef HAVE_INITRAMFS
    /* Preferred: Load from initramfs CPIO archive */
    kprintf("Loading from initramfs...\n");
    extern int initramfs_init(void);
    int count = initramfs_init();
    if (count > 0) {
        kprintf("Loaded %d files\n", count);
        return count;
    }
#endif

#ifdef USE_EMBEDDED_BINS
    /* Legacy fallback: embedded binaries */
    kprintf("Installing embedded binaries...\n");
    int embedded_count = 0;
    
#ifdef HAVE_LDSO
    if (install_binary("/lib/ld.so", lib_ldso, sizeof(lib_ldso)) == 0) embedded_count++;
#endif
#ifdef HAVE_LIBC_SO
    if (install_binary("/lib/libc.so", lib_libc_so, sizeof(lib_libc_so)) == 0) embedded_count++;
#endif
    if (install_binary("/bin/ls", bin_ls, sizeof(bin_ls)) == 0) embedded_count++;
    if (install_binary("/bin/mkdir", bin_mkdir, sizeof(bin_mkdir)) == 0) embedded_count++;
    if (install_binary("/bin/cat", bin_cat, sizeof(bin_cat)) == 0) embedded_count++;
    if (install_binary("/bin/rm", bin_rm, sizeof(bin_rm)) == 0) embedded_count++;
    if (install_binary("/bin/pwd", bin_pwd, sizeof(bin_pwd)) == 0) embedded_count++;
    if (install_binary("/bin/echo", bin_echo, sizeof(bin_echo)) == 0) embedded_count++;
    if (install_binary("/bin/touch", bin_touch, sizeof(bin_touch)) == 0) embedded_count++;
    if (install_binary("/bin/sh", bin_sh, sizeof(bin_sh)) == 0) embedded_count++;
    
    kprintf("Installed %d binaries\n", embedded_count);
    return embedded_count;
#else
    kprintf("No embedded binaries (initramfs required)\n");
    return 0;
#endif
}
