#include <stdint.h>

typedef struct {
    int counter;
    const char *message;
} module_state_t;

#define MODULE_API_VERSION 1

struct testmod_api {
    uint32_t version;
    int (*init)(void *ctx);
    void (*tick)(void *ctx);
    const char* (*get_message)(void *ctx);
    void (*shutdown)(void *ctx);
};

static int mod_init(void *ctx) {
    module_state_t *state = (module_state_t *)ctx;
    if (state) {
        state->counter = 0;
        state->message = "Hello from testmod v1!";
    }
    return 0;
}

static void mod_tick(void *ctx) {
    module_state_t *state = (module_state_t *)ctx;
    if (state) {
        state->counter++;
    }
}

static const char* mod_get_message(void *ctx) {
    module_state_t *state = (module_state_t *)ctx;
    return state ? state->message : "No state";
}

static void mod_shutdown(void *ctx) {
    (void)ctx;
}

struct testmod_api module_api = {
    .version     = MODULE_API_VERSION,
    .init        = mod_init,
    .tick        = mod_tick,
    .get_message = mod_get_message,
    .shutdown    = mod_shutdown
};
