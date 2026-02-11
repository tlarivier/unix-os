#include <kernel/process.h>
#include <kernel/memory.h>
#include <kernel/paging.h>
#include <kernel/errno.h>
#include <kernel/spinlock.h>
#include <stdint.h>
#include <stddef.h>

#define IPC_PRIVATE     0
#define IPC_CREAT       01000
#define IPC_EXCL        02000
#define IPC_RMID        0
#define IPC_STAT        2

#define SHMMNI          64      
#define SHMMAX          (4*1024*1024)  
#define SHMMIN          1       

typedef struct shm_segment {
    int             id;         
    int             key;        
    size_t          size;       
    uint32_t        phys_base;  
    int             nattach;    
    uid_t           uid;        
    gid_t           gid;        
    uint32_t        mode;       
    int             in_use;     
    int             marked_destroy; 
} shm_segment_t;

static shm_segment_t shm_table[SHMMNI];
static spinlock_t shm_lock;
static int shm_next_id = 1;
static int shm_initialized = 0;

void shm_init(void) {
    if (shm_initialized) return;
    
    spinlock_init(&shm_lock, "shm");
    for (int i = 0; i < SHMMNI; i++) {
        shm_table[i].in_use = 0;
        shm_table[i].id = 0;
    }
    shm_initialized = 1;
}

static shm_segment_t* shm_find_by_key(int key) {
    for (int i = 0; i < SHMMNI; i++) {
        if (shm_table[i].in_use && shm_table[i].key == key) {
            return &shm_table[i];
        }
    }
    return NULL;
}

static shm_segment_t* shm_find_by_id(int shmid) {
    for (int i = 0; i < SHMMNI; i++) {
        if (shm_table[i].in_use && shm_table[i].id == shmid) {
            return &shm_table[i];
        }
    }
    return NULL;
}

static shm_segment_t* shm_alloc_slot(void) {
    for (int i = 0; i < SHMMNI; i++) {
        if (!shm_table[i].in_use) {
            shm_table[i].in_use = 1;
            shm_table[i].id = shm_next_id++;
            return &shm_table[i];
        }
    }
    return NULL;
}

int32_t sys_shmget(uint32_t key, uint32_t size, uint32_t shmflg, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    if (!shm_initialized) shm_init();
    
    process_t* proc = get_current_process();
    if (!proc) return -ESRCH;
    
    if (size > SHMMAX || size < SHMMIN) {
        return -EINVAL;
    }
    
    spin_lock(&shm_lock);
    
    if (key != IPC_PRIVATE) {
        shm_segment_t* existing = shm_find_by_key((int)key);
        if (existing) {
            if ((shmflg & IPC_CREAT) && (shmflg & IPC_EXCL)) {
                spin_unlock(&shm_lock);
                return -EEXIST;
            }
            int id = existing->id;
            spin_unlock(&shm_lock);
            return id;
        }
        
        if (!(shmflg & IPC_CREAT)) {
            spin_unlock(&shm_lock);
            return -ENOENT;
        }
    }
    
    shm_segment_t* seg = shm_alloc_slot();
    if (!seg) {
        spin_unlock(&shm_lock);
        return -ENOSPC;
    }
    
    size_t aligned_size = (size + 4095) & ~4095;
    size_t num_pages    = aligned_size / 4096;
    
    uint32_t phys_base = 0;
    for (size_t i = 0; i < num_pages; i++) {
        uint32_t frame = allocate_frame();
        if (frame == (uint32_t)-1) {
            /* Rollback */
            for (size_t j = 0; j < i; j++) {
                free_frame(phys_base + j * 4096);
            }
            seg->in_use = 0;
            spin_unlock(&shm_lock);
            return -ENOMEM;
        }
        if (i == 0) phys_base = frame;
        zero_frame(frame);
    }
    
    seg->key       = (int)key;
    seg->size      = size;
    seg->phys_base = phys_base;
    seg->nattach   = 0;
    seg->uid       = proc->euid;
    seg->gid       = proc->egid;
    seg->mode      = shmflg & 0777;
    seg->marked_destroy = 0;
    
    int id = seg->id;
    spin_unlock(&shm_lock);
    
    return id;
}

int32_t sys_shmat(uint32_t shmid, uint32_t shmaddr, uint32_t shmflg, uint32_t u4, uint32_t u5) {
    (void)shmflg; (void)u4; (void)u5;
    
    if (!shm_initialized) return -EINVAL;
    
    process_t* proc = get_current_process();
    if (!proc || !proc->memory) return -ESRCH;
    
    spin_lock(&shm_lock);
    
    shm_segment_t* seg = shm_find_by_id((int)shmid);
    if (!seg) {
        spin_unlock(&shm_lock);
        return -EINVAL;
    }
    
    uint32_t attach_addr;
    if (shmaddr != 0) {
        attach_addr = shmaddr & ~4095;
    } else {
        if (proc->memory->mmap_next_addr == 0) {
            proc->memory->mmap_next_addr = 0x40000000;
        }
        attach_addr = proc->memory->mmap_next_addr;
        proc->memory->mmap_next_addr += (seg->size + 4095) & ~4095;
    }
    
    size_t aligned_size = (seg->size + 4095) & ~4095;
    size_t num_pages = aligned_size / 4096;
    
    for (size_t i = 0; i < num_pages; i++) {
        uint32_t vaddr = attach_addr + i * 4096;
        uint32_t paddr = seg->phys_base + i * 4096;
        uint32_t flags = PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE;
        map_page_ext(proc->memory->page_directory, vaddr, paddr, flags);
    }
    
    seg->nattach++;
    
    spin_unlock(&shm_lock);
    
    return (int32_t)attach_addr;
}

int32_t sys_shmdt(uint32_t shmaddr, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    
    if (!shm_initialized) return -EINVAL;
    
    process_t* proc = get_current_process();
    if (!proc || !proc->memory) return -ESRCH;
    
    if (shmaddr == 0 || (shmaddr & 4095)) {
        return -EINVAL;
    }
    
    spin_lock(&shm_lock);
    
    uint32_t paddr = get_physical_addr(proc->memory->page_directory, shmaddr);
    if (!paddr) {
        spin_unlock(&shm_lock);
        return -EINVAL;
    }
    
    shm_segment_t* seg = NULL;
    for (int i = 0; i < SHMMNI; i++) {
        if (shm_table[i].in_use && shm_table[i].phys_base == paddr) {
            seg = &shm_table[i];
            break;
        }
    }
    
    if (!seg) {
        spin_unlock(&shm_lock);
        return -EINVAL;
    }
    
    size_t aligned_size = (seg->size + 4095) & ~4095;
    size_t num_pages    = aligned_size / 4096;
    
    for (size_t i = 0; i < num_pages; i++) {
        unmap_page_ext(proc->memory->page_directory, shmaddr + i * 4096);
    }
    
    seg->nattach--;
    
    if (seg->marked_destroy && seg->nattach == 0) {
        for (size_t i = 0; i < num_pages; i++) {
            free_frame(seg->phys_base + i * 4096);
        }
        seg->in_use = 0;
    }
    
    spin_unlock(&shm_lock);
    
    return 0;
}

int32_t sys_shmctl(uint32_t shmid, uint32_t cmd, uint32_t buf, uint32_t u4, uint32_t u5) {
    (void)buf; (void)u4; (void)u5;
    
    if (!shm_initialized) return -EINVAL;
    
    spin_lock(&shm_lock);
    
    shm_segment_t* seg = shm_find_by_id((int)shmid);
    if (!seg) {
        spin_unlock(&shm_lock);
        return -EINVAL;
    }
    
    switch (cmd) {
        case IPC_RMID:
            seg->marked_destroy = 1;
            if (seg->nattach == 0) {
                /* Destroy immediately */
                size_t aligned_size = (seg->size + 4095) & ~4095;
                size_t num_pages = aligned_size / 4096;
                for (size_t i = 0; i < num_pages; i++) {
                    free_frame(seg->phys_base + i * 4096);
                }
                seg->in_use = 0;
            }
            break;
            
        case IPC_STAT:
            /* TODO: Copy stats to user buffer */
            spin_unlock(&shm_lock);
            return -ENOSYS;
            
        default:
            spin_unlock(&shm_lock);
            return -EINVAL;
    }
    
    spin_unlock(&shm_lock);
    return 0;
}
