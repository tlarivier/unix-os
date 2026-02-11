#include <stddef.h>
#include <stdint.h>

static char dl_error_msg[128] = "";

/* ld.so interface - weak symbols resolved by ld.so at load time */
extern void *__rtld_dlopen(const char *filename, int flags) __attribute__((weak));
extern int __rtld_dlclose(void *handle) __attribute__((weak));
extern void *__rtld_dlsym(void *handle, const char *symbol) __attribute__((weak));

static void set_error(const char *msg) {
    int i = 0;
    while (msg[i] && i < 127) {
        dl_error_msg[i] = msg[i];
        i++;
    }
    dl_error_msg[i] = '\0';
}

void *dlopen(const char *filename, int flags) {
    dl_error_msg[0] = '\0';
    
    if (!filename) {
        /* Return handle to main program */
        return (void *)1;
    }
    
    if (!__rtld_dlopen) {
        set_error("dlopen: ld.so not loaded (static binary)");
        return NULL;
    }
    
    void *ret = __rtld_dlopen(filename, flags);
    if (!ret) {
        set_error("dlopen: failed to load library");
    }
    return ret;
}

int dlclose(void *handle) {
    dl_error_msg[0] = '\0';
    
    if (!handle || handle == (void *)1) {
        return 0;
    }
    
    if (!__rtld_dlclose) {
        set_error("dlclose: ld.so not loaded (static binary)");
        return -1;
    }
    
    return __rtld_dlclose(handle);
}

void *dlsym(void *handle, const char *symbol) {
    dl_error_msg[0] = '\0';
    
    if (!symbol) {
        set_error("dlsym: NULL symbol");
        return NULL;
    }
    
    if (!__rtld_dlsym) {
        set_error("dlsym: ld.so not loaded (static binary)");
        return NULL;
    }
    
    void *ret = __rtld_dlsym(handle, symbol);
    if (!ret) {
        set_error("dlsym: symbol not found");
    }
    return ret;
}

char *dlerror(void) {
    if (dl_error_msg[0] == '\0') {
        return NULL;
    }
    return dl_error_msg;
}
