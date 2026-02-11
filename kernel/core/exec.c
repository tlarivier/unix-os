#include <kernel/process.h>
#include <kernel/memory.h>
#include <kernel/errno.h>
#include <kernel/kernel.h>
#include <kernel/uaccess.h>
#include <kernel/vfs.h>
#include <kernel/elf_loader.h>
#include <kernel/elf.h>
#include <kernel/kstring.h>
#include <../uapi/signal.h>
#include <../uapi/syscalls.h>
#include <kernel/types.h>

#define MAX_ARG_LEN      4096
#define EXEC_STACK_SIZE  (128 * 1024)  

static int copy_to_user_phys(process_memory_t *mem, uint32_t user_addr, const void *src, size_t len) {
    if (!mem || !mem->page_directory) return -EINVAL;
    
    const uint8_t *s = (const uint8_t *)src;
    
    while (len > 0) {
        uint32_t phys = get_physical_addr(mem->page_directory, user_addr);
        if (!phys) return -EFAULT;
        
        uint32_t frame = phys & 0xFFFFF000;
        uint32_t offset = phys & 0xFFF;
        uint32_t chunk = PAGE_SIZE_CONST - offset;
        if (chunk > len) chunk = len;
        
        /* Write to physical frame - use temp mapping for high memory */
        uint8_t *dst = (uint8_t *)copy_to_frame(frame, offset, s, chunk);
        if (!dst) return -EFAULT;
        
        user_addr += chunk;
        s += chunk;
        len -= chunk;
    }
    return 0;
}
#define MAX_EXEC_ARGS    64   
#define MAX_EXEC_ENVS    32   

static int copy_exec_args(char *const argv[], char **kernel_argv, int max_args) {
    if (!argv || !kernel_argv) return 0;
    int argc = 0;
    for (int i = 0; i < max_args; i++) {
        char* user_ptr = NULL;
        int ret = copy_from_user(&user_ptr, &argv[i], sizeof(char*));
        if (IS_ERROR(ret)) return ret;
        
        if (user_ptr == NULL) break;
        
        kernel_argv[i] = kmalloc(MAX_ARG_LEN);
        if (!kernel_argv[i]) {
            for (int j = 0; j < i; j++) kfree(kernel_argv[j]);
            return -ENOMEM;
        }
        ret = copy_from_user(kernel_argv[i], user_ptr, MAX_ARG_LEN - 1);
        if (IS_ERROR(ret)) {
            kfree(kernel_argv[i]);
            for (int j = 0; j < i; j++) kfree(kernel_argv[j]);
            return ret;
        }
        kernel_argv[i][MAX_ARG_LEN - 1] = '\0';
        argc++;
    }
    return argc;
}

static void cleanup_exec_args(char **kernel_args, int count) {
    if (!kernel_args) return;
    
    for (int i = 0; i < count; i++) {
        if (kernel_args[i]) {
            kfree(kernel_args[i]);
            kernel_args[i] = NULL;
        }
    }
}

static int setup_exec_user_stack(process_t *proc, char **argv, int argc, char **envp, int envc) {
    if (!proc || !proc->memory) return -EINVAL;
    
    process_memory_t *mem = proc->memory;
    uint32_t stack_top = USER_STACK_BASE - 4096;
    
    size_t strings_size = 0;
    for (int i = 0; i < argc; i++)
        if (argv[i]) strings_size += kstrlen(argv[i]) + 1;
    for (int i = 0; i < envc; i++)
        if (envp[i]) strings_size += kstrlen(envp[i]) + 1;
    
    int auxv_count = (proc->interp_base != 0) ? 14 : 0;  /* 7 pairs for dynamic linking */
    
    uint32_t strings_addr = (stack_top - strings_size) & ~3;
    uint32_t auxv_addr    = strings_addr - auxv_count * sizeof(uint32_t);
    uint32_t envp_start   = auxv_addr    - (envc + 1) * sizeof(uint32_t);
    uint32_t argv_start   = envp_start   - (argc + 1) * sizeof(uint32_t);
    uint32_t argc_addr    = argv_start   - sizeof(uint32_t);
    argc_addr &= ~0xF;  /* 16-byte align */
    
    argv_start   = argc_addr  + sizeof(uint32_t);
    envp_start   = argv_start + (argc + 1) * sizeof(uint32_t);
    auxv_addr    = envp_start + (envc + 1) * sizeof(uint32_t);
    strings_addr = auxv_addr  + auxv_count * sizeof(uint32_t);
    
    uint32_t val = (uint32_t)argc;
    if (copy_to_user_phys(mem, argc_addr, &val, sizeof(val)) < 0) return -EFAULT;
    
    uint32_t str_ptr = strings_addr;
    for (int i = 0; i < argc; i++) {
        if (argv[i]) {
            size_t len = kstrlen(argv[i]) + 1;
            if (copy_to_user_phys(mem, str_ptr, argv[i], len) < 0) return -EFAULT;
            if (copy_to_user_phys(mem, argv_start + i * sizeof(uint32_t), &str_ptr, sizeof(uint32_t)) < 0) return -EFAULT;
            str_ptr += len;
        }
    }
    val = 0;
    if (copy_to_user_phys(mem, argv_start + argc * sizeof(uint32_t), &val, sizeof(val)) < 0) return -EFAULT;
    
    /* Write envp pointers and strings */
    for (int i = 0; i < envc; i++) {
        if (envp[i]) {
            size_t len = kstrlen(envp[i]) + 1;
            if (copy_to_user_phys(mem, str_ptr, envp[i], len) < 0) return -EFAULT;
            if (copy_to_user_phys(mem, envp_start + i * sizeof(uint32_t), &str_ptr, sizeof(uint32_t)) < 0) return -EFAULT;
            str_ptr += len;
        }
    }
    val = 0;
    if (copy_to_user_phys(mem, envp_start + envc * sizeof(uint32_t), &val, sizeof(val)) < 0) return -EFAULT;
    
    /* Write auxv for dynamic linking */
    if (proc->interp_base != 0) {
        uint32_t auxv[14];
        auxv[0]  = AT_PHDR;    auxv[1] = proc->elf_phdr;
        auxv[2]  = AT_PHENT;   auxv[3] = sizeof(Elf32_Phdr);
        auxv[4]  = AT_PHNUM;   auxv[5] = proc->elf_phnum;
        auxv[6]  = AT_PAGESZ;  auxv[7] = PAGE_SIZE_CONST;
        auxv[8]  = AT_BASE;    auxv[9] = proc->interp_base;
        auxv[10] = AT_ENTRY;  auxv[11] = proc->elf_entry;
        auxv[12] = AT_NULL;   auxv[13] = 0;
        if (copy_to_user_phys(mem, auxv_addr, auxv, sizeof(auxv)) < 0) return -EFAULT;
    }
    
    if (proc->interp_base != 0) {
        /* Dynamic binary: ld.so expects argc at esp */
        proc->context.esp = argc_addr;
        proc->context.ebp = argc_addr;
    } else {
        /* 
         * Static binary: crt0 expects:
         *   esp+0:  return address (0)
         *   esp+4:  argc
         *   esp+8:  argv (pointer to argv array)
         *   esp+12: envp (pointer to envp array)
         */
        uint32_t frame_base = argc_addr - 16;  /* Space for ret, argc, argv, envp */
        frame_base &= ~0xF;  
        
        val = 0;  
        if (copy_to_user_phys(mem, frame_base, &val, sizeof(val)) < 0) return -EFAULT;
        val = (uint32_t)argc;
        if (copy_to_user_phys(mem, frame_base + 4, &val, sizeof(val)) < 0) return -EFAULT;
        val = argv_start;  
        if (copy_to_user_phys(mem, frame_base + 8, &val, sizeof(val)) < 0) return -EFAULT;
        val = envp_start; 
        if (copy_to_user_phys(mem, frame_base + 12, &val, sizeof(val)) < 0) return -EFAULT;
        
        proc->context.esp = frame_base;
        proc->context.ebp = frame_base;
    }
    
    return 0;
}

int sys_execve(const char *pathname, char *const argv[], char *const envp[]) {
    uint32_t flags;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags));
    
    if (!pathname) {
        __asm__ volatile("pushl %0; popfl" : : "r"(flags));
        return -EINVAL;
    }
    
    process_t *current = get_current_process();
    if (!current) {
        __asm__ volatile("pushl %0; popfl" : : "r"(flags));
        return -ESRCH;
    }
    
    int ret;
    
    char *kernel_path = kmalloc(256);
    if (!kernel_path) {
        __asm__ volatile("pushl %0; popfl" : : "r"(flags));
        return -ENOMEM;
    }
    
    ret = copy_from_user(kernel_path, pathname, 255);
    if (IS_ERROR(ret)) {
        kfree(kernel_path);
        __asm__ volatile("sti");
        return ret;
    }
    kernel_path[255] = '\0';
    
    if (kernel_path[0] != '/') {
        kfree(kernel_path);
        __asm__ volatile("sti");
        return -ENOENT;
    }
    
    char **kernel_argv = kmalloc((MAX_EXEC_ARGS + 1) * sizeof(char*));
    if (!kernel_argv) {
        kfree(kernel_path);
        __asm__ volatile("sti");
        return -ENOMEM;
    }
    memset(kernel_argv, 0, (MAX_EXEC_ARGS + 1) * sizeof(char*));
    
    int argc = copy_exec_args(argv, kernel_argv, MAX_EXEC_ARGS);
    if (argc < 0) {
        kfree(kernel_argv);
        kfree(kernel_path);
        __asm__ volatile("sti");
        return argc;
    }
    
    char **kernel_envp = kmalloc((MAX_EXEC_ENVS + 1) * sizeof(char*));
    if (!kernel_envp) {
        cleanup_exec_args(kernel_argv, argc);
        kfree(kernel_argv);
        kfree(kernel_path);
        __asm__ volatile("sti");
        return -ENOMEM;
    }
    memset(kernel_envp, 0, (MAX_EXEC_ENVS + 1) * sizeof(char*));
    
    int envc = copy_exec_args(envp, kernel_envp, MAX_EXEC_ENVS);
    if (envc < 0) {
        ret = envc;
        goto cleanup_argv;
    }
    
    vfs_node_t *file_node = vfs_resolve_path(kernel_path);
    if (!file_node) {
        kprintf("EXEC: file not found!\n");
        ret = -ENOENT;
        goto cleanup_envp;
    }
    
    process_memory_t *old_memory = current->memory;
    
    process_memory_t *new_memory = create_process_memory();
    if (!new_memory) {
        ret = -ENOMEM;
        goto cleanup_envp;
    }
    
    current->memory          = new_memory;
    current->user_stack_base = new_memory->stack_base;
    
    extern uint32_t kernel_page_directory[];
    uint32_t saved_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(saved_cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"((uint32_t)kernel_page_directory) : "memory");
    
    ret = load_elf_process(kernel_path, current);
    if (IS_ERROR(ret)) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(saved_cr3) : "memory");
        destroy_process_memory(new_memory);
        current->memory = old_memory;
        goto cleanup_envp;
    }
    
    ret = setup_exec_user_stack(current, kernel_argv, argc, kernel_envp, envc);
    
    __asm__ volatile("mov %0, %%cr3" : : "r"(saved_cr3) : "memory");
    
    if (IS_ERROR(ret)) {
        /* Stack setup failed - rollback */
        destroy_process_memory(new_memory);
        current->memory = old_memory;
        goto cleanup_envp;
    }
    
    const char *program_name = kernel_path;
    for (int i = 0; kernel_path[i]; i++) {
        if (kernel_path[i] == '/') {
            program_name = &kernel_path[i + 1];
        }
    }
    int name_len = 0;
    while (program_name[name_len] && name_len < (int)(sizeof(current->name) - 1)) {
        current->name[name_len] = program_name[name_len];
        name_len++;
    }
    current->name[name_len] = '\0';  
    
    for (int i = 0; i < MAX_OPEN_FILES_CONST; i++) {
        if (current->fd_table[i].node_idx != 0 && 
            (current->fd_table[i].flags & O_CLOEXEC)) {
            vfs_close(i);
        }
    }
    
    for (int i = 1; i < NSIG; i++) {
        if (current->signal_handlers[i] != SIG_IGN) {
            current->signal_handlers[i] = SIG_DFL;
        }
    }
    
    current->signal_mask = 0;
    current->signal_pending = 0;
    
    destroy_process_memory(old_memory);
    
    cleanup_exec_args(kernel_argv, argc);
    cleanup_exec_args(kernel_envp, envc);
    kfree(kernel_argv);
    kfree(kernel_envp);
    kfree(kernel_path);
    
    switch_page_directory(current->memory->page_directory);
    
    tss_set_kernel_stack((uint32_t)current->kernel_stack + KERNEL_STACK_SIZE);
    
    extern void jump_to_usermode(uint32_t entry, uint32_t stack);
    jump_to_usermode(current->context.eip, current->context.esp);
    
    return 0;

cleanup_envp:
    cleanup_exec_args(kernel_envp, envc);
    kfree(kernel_envp);
cleanup_argv:
    cleanup_exec_args(kernel_argv, argc);
    kfree(kernel_argv);
    kfree(kernel_path);
    __asm__ volatile("sti");
    return ret;
}

int execl(const char *path, const char *arg0, ...) {
    char *argv[] = {(char*)arg0, NULL};
    char *envp[] = {NULL};
    
    return sys_execve(path, argv, envp);
}

int execv(const char *path, char *const argv[]) {
    char *envp[] = {NULL};
    return sys_execve(path, argv, envp);
}
