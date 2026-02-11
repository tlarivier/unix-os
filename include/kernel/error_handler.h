#ifndef KERNEL_ERROR_HANDLER_H
#define KERNEL_ERROR_HANDLER_H

#include <stdint.h>

#define MAX_ERROR_LOG 64

typedef struct kernel_error {
    int32_t code;
    char message[128];
    uint32_t timestamp;
    uint32_t line;
    char file[64];
    uint32_t process_id;
} kernel_error_t;

enum error_severity {
    ERROR_INFO    = 0,
    ERROR_WARNING = 1,
    ERROR_ERROR   = 2,
    ERROR_FATAL   = 3,
    ERROR_PANIC   = 4
};

void log_kernel_error(int32_t code, const char *message, const char *file, uint32_t line);
void show_error_log(void);
void clear_error_log(void);

#endif 
