#include <kernel/kernel.h>
#include <kernel/errno.h>
#include <kernel/error_handler.h>
#include <kernel/constants.h>
#include <kernel/kstring.h>

static kernel_error_t error_log[MAX_ERROR_LOG];
static uint32_t error_count = 0;
static uint32_t error_index = 0;
static uint32_t panic_count = 0;
static uint32_t warning_count = 0;

#include <kernel/timer.h>

void kernel_error_init(void) {
    error_count = 0;
    error_index = 0;
    panic_count = 0;
    warning_count = 0;
    for (int i = 0; i < MAX_ERROR_LOG; i++) {
        error_log[i].code = 0;
        error_log[i].message[0] = '\0';
        error_log[i].file[0] = '\0';
        error_log[i].line = 0;
        error_log[i].timestamp = 0;
        error_log[i].process_id = 0;
    }
}

int32_t kernel_error_report(int32_t code, const char* message, const char* file, uint32_t line) {
    uint32_t current_pid = 0;
    kernel_error_t* error = &error_log[error_index];
    error->code = code;
    kstrncpy(error->message, message, sizeof(error->message));
    kstrncpy(error->file, file, sizeof(error->file));
    error->line = line;
    error->timestamp = get_timer_ticks();
    error->process_id = current_pid;
    error_count++;
    error_index = (error_index + 1) % MAX_ERROR_LOG;
    if (IS_ERROR(code)) {
        kprintf("ERROR [%d]: %s (%s:%u)\n", code, message, file, line);
    }
    return code;
}

void kernel_warning(const char* message, const char* file, uint32_t line) {
    warning_count++;
    kernel_error_report(-EIO, message, file, line);
    kprintf("WARNING: %s (%s:%u)\n", message, file, line);
}

void kernel_panic(const char* message, const char* file, uint32_t line) {
    __asm__ volatile("cli");
    panic_count++;
    vga_print_at("*** KERNEL PANIC ***", 30, 10, 0x0F);
    vga_print_at("System halted due to fatal error", 23, 12, 0x0E);
    
    vga_print_at("Error: ", 10, 14, 0x0F);
    vga_print_at(message, 17, 14, 0x0A);
    
    vga_print_at("File: ", 10, 15, 0x0F);
    vga_print_at(file, 16, 15, 0x0A);
    char line_str[16];
    uint32_t temp_line = line;
    int i = 0;
    if (temp_line == 0) {
        line_str[i++] = '0';
    } else {
        while (temp_line > 0) {
            line_str[i++] = '0' + (temp_line % 10);
            temp_line /= 10;
        }
    }
    line_str[i] = '\0';
    for (int j = 0; j < i / 2; j++) {
        char temp = line_str[j];
        line_str[j] = line_str[i - 1 - j];
        line_str[i - 1 - j] = temp;
    }
    
    vga_print_at("Line: ", 10, 16, 0x0F);
    vga_print_at(line_str, 16, 16, 0x0A);
    
    vga_print_at("System state: HALTED", 25, 18, 0x0C);
    vga_print_at("Please restart the system", 23, 20, 0x07);
    while(1) {
        __asm__ volatile("hlt");
    }
}

const char* error_to_string(int error_code) {
    switch (error_code) {
        case 0:           return "Success";
        case -EPERM:      return "Operation not permitted";
        case -ENOENT:     return "No such file or directory";
        case -ESRCH:      return "No such process";
        case -EINTR:      return "Interrupted system call";
        case -EIO:        return "I/O error";
        case -ENXIO:      return "No such device or address";
        case -E2BIG:      return "Argument list too long";
        case -ENOEXEC:    return "Exec format error";
        case -EBADF:      return "Bad file number";
        case -ECHILD:     return "No child processes";
        case -EAGAIN:     return "Try again";
        case -ENOMEM:     return "Out of memory";
        case -EACCES:     return "Permission denied";
        case -EFAULT:     return "Bad address";
        case -ENOTBLK:    return "Block device required";
        case -EBUSY:      return "Device or resource busy";
        case -EEXIST:     return "File exists";
        case -EXDEV:      return "Cross-device link";
        case -ENODEV:     return "No such device";
        case -ENOTDIR:    return "Not a directory";
        case -EISDIR:     return "Is a directory";
        case -EINVAL:     return "Invalid argument";
        case -ENFILE:     return "File table overflow";
        case -EMFILE:     return "Too many open files";
        case -ENOTTY:     return "Not a typewriter";
        case -ETXTBSY:    return "Text file busy";
        case -EFBIG:      return "File too large";
        case -ENOSPC:     return "No space left on device";
        case -ESPIPE:     return "Illegal seek";
        case -EROFS:      return "Read-only file system";
        case -EMLINK:     return "Too many links";
        case -EPIPE:      return "Broken pipe";
        case -EDOM:       return "Math argument out of domain";
        case -ERANGE:     return "Math result not representable";
        case -EDEADLK:    return "Resource deadlock would occur";
        case -ENAMETOOLONG: return "File name too long";
        case -ENOLCK:     return "No record locks available";
        case -ENOSYS:     return "Function not implemented";
        case -ENOTEMPTY:  return "Directory not empty";
        case -ELOOP:      return "Too many symbolic links";
        case -ENOMSG:     return "No message of desired type";
        case -EIDRM:      return "Identifier removed";
        case -EOVERFLOW:  return "Value too large for defined data type";
        case -EILSEQ:     return "Illegal byte sequence";
        case -ETIMEDOUT:  return "Connection timed out";

        case -ENOTSOCK:   return "Socket operation on non-socket";
        case -ECONNRESET: return "Connection reset by peer";
        case -ECONNREFUSED: return "Connection refused";
        case -EHOSTUNREACH: return "No route to host";
        default:          return "Unknown error";
    }
}

void show_error_statistics(void) {
    kprintf("\n=== KERNEL ERROR STATISTICS ===\n");
    kprintf("Total errors: %u\n", error_count);
    kprintf("Warnings: %u\n", warning_count);
    kprintf("Panics: %u\n", panic_count);
    
    if (error_count > 0) {
        kprintf("\nRecent errors:\n");
        uint32_t start = (error_index >= 10) ? error_index - 10 : 0;
        uint32_t count = (error_count < 10) ? error_count : 10;
        
        for (uint32_t i = 0; i < count; i++) {
            uint32_t idx = (start + i) % 64;
            kernel_error_t* err = &error_log[idx];
            
            if (err->code != 0) {
                kprintf("  [%u] %s: %s (%s:%u)\n", 
                    err->timestamp, 
                    error_to_string(err->code),
                    err->message,
                    err->file,
                    err->line);
            }
        }
    }
    kprintf("\n");
}

int32_t validate_memory_range(void* ptr, size_t size, uint32_t expected_flags) {
    if (!ptr) return -EINVAL;
    
    uint32_t addr = (uint32_t)ptr;
    
    if (addr + size < addr) return -EOVERFLOW;
    if (addr & 0x3) return -EFAULT;
    
    if (expected_flags & 0x1 && (addr < USER_SPACE_START || addr >= USER_SPACE_END)) return -EACCES;
    else if (addr < KERNEL_SPACE_START || addr >= KERNEL_SPACE_END) return -EACCES;
    
    return 0;
}

#ifdef DEBUG
void test_error_handling(void) {
    kprintf("Testing kernel error handling...\n");
    KERNEL_ERROR(-ENOMEM, "Test memory allocation failure");
    KERNEL_WARNING("Test warning message");
    KERNEL_ERROR(-EINVAL, "Test invalid parameter error");
    int32_t result = validate_memory_range(NULL, 100, 0);
    if (IS_ERROR(result)) {
        KERNEL_ERROR(result, "Memory validation test failed as expected");
    }
    show_error_statistics();
    kprintf("Error handling test completed\n");
}
#endif
