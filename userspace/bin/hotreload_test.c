#include <sys/types.h>
#include <syscall.h>
#include <stdio.h>
#include <unistd.h>

/* Module API structure (must match testmod.c) */
struct testmod_api {
    uint32_t version;
    int (*init)(void *ctx);
    void (*tick)(void *ctx);
    const char* (*get_message)(void *ctx);
    void (*shutdown)(void *ctx);
};

/* Module state */
typedef struct {
    int counter;
    const char *message;
} module_state_t;

/* dlfcn declarations */
extern void *dlopen(const char *filename, int flags);
extern void *dlsym(void *handle, const char *symbol);
extern int dlclose(void *handle);
extern char *dlerror(void);

#define RTLD_NOW 2

/* Direct syscall test */
extern int open(const char* path, int flags, ...);
extern int read(int fd, void* buf, int count);
extern int close(int fd);

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    
    puts("=== Hot Reload Test ===");
    
    /* First test: direct read to verify syscall works */
    int fd = open("/lib/testmod.so", 0);
    if (fd >= 0) {
        unsigned char buf[8] = {0};
        int r = read(fd, buf, 8);
        puts("Direct read test:");
        if (r > 0) {
            char hex[32];
            for (int i = 0; i < 4 && i < r; i++) {
                hex[i*3]   = "0123456789ABCDEF"[(buf[i] >> 4) & 0xF];
                hex[i*3+1] = "0123456789ABCDEF"[buf[i]        & 0xF];
                hex[i*3+2] = ' ';
            }
            hex[12] = '\0';
            puts(hex);
        }
        close(fd);
    }
    
    /* Load the module */
    void *handle = dlopen("/lib/testmod.so", RTLD_NOW);
    if (!handle) {
        puts("Failed to load testmod.so");
        char *err = dlerror();
        if (err) puts(err);
        return 1;
    }
    puts("Module loaded!");
    
    /* Get API */
    struct testmod_api *api = (struct testmod_api *)dlsym(handle, "module_api");
    if (!api) {
        puts("Failed to find module_api symbol");
        dlclose(handle);
        return 1;
    }
    puts("API found!");
    
    /* Initialize module with state */
    module_state_t state = {0};
    if (api->init(&state) != 0) {
        puts("Module init failed");
        dlclose(handle);
        return 1;
    }
    
    /* Use the module */
    puts("Calling tick 3 times...");
    api->tick(&state);
    api->tick(&state);
    api->tick(&state);
    
    /* Get message */
    const char *msg = api->get_message(&state);
    puts("Message: ");
    puts(msg);
    
    /* Cleanup */
    api->shutdown(&state);
    dlclose(handle);
    
    puts("=== Test Complete ===");
    return 0;
}
