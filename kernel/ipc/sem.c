#include <kernel/process.h>
#include <kernel/memory.h>
#include <kernel/errno.h>
#include <kernel/spinlock.h>
#include <stdint.h>
#include <stddef.h>

#define IPC_PRIVATE     0
#define IPC_CREAT       01000
#define IPC_EXCL        02000
#define IPC_NOWAIT      04000
#define IPC_RMID        0
#define IPC_STAT        2
#define SETVAL          16
#define GETVAL          12

#define SEMMNI          64      
#define SEMMSL          32      

typedef struct sembuf {
    uint16_t sem_num;   
    int16_t  sem_op;    
    int16_t  sem_flg;   
} sembuf_t;

typedef struct sem {
    int16_t  semval;    
    pid_t    sempid;    
} sem_t;

typedef struct sem_set {
    int         id;         
    int         key;        
    int         nsems;      
    sem_t       sems[SEMMSL];
    uid_t       uid;        
    gid_t       gid;        
    uint32_t    mode;       
    int         in_use;     
} sem_set_t;

static sem_set_t sem_table[SEMMNI];
static spinlock_t sem_lock;
static int sem_next_id = 1;
static int sem_initialized = 0;

void sem_init(void) {
    if (sem_initialized) return;
    
    spinlock_init(&sem_lock, "sem");
    for (int i = 0; i < SEMMNI; i++) {
        sem_table[i].in_use = 0;
        sem_table[i].id = 0;
    }
    sem_initialized = 1;
}

static sem_set_t* sem_find_by_key(int key) {
    for (int i = 0; i < SEMMNI; i++) {
        if (sem_table[i].in_use && sem_table[i].key == key) {
            return &sem_table[i];
        }
    }
    return NULL;
}

static sem_set_t* sem_find_by_id(int semid) {
    for (int i = 0; i < SEMMNI; i++) {
        if (sem_table[i].in_use && sem_table[i].id == semid) {
            return &sem_table[i];
        }
    }
    return NULL;
}

static sem_set_t* sem_alloc_slot(void) {
    for (int i = 0; i < SEMMNI; i++) {
        if (!sem_table[i].in_use) {
            sem_table[i].in_use = 1;
            sem_table[i].id = sem_next_id++;
            return &sem_table[i];
        }
    }
    return NULL;
}

int32_t sys_semget(uint32_t key, uint32_t nsems, uint32_t semflg, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    if (!sem_initialized) sem_init();
    
    process_t* proc = get_current_process();
    if (!proc) return -ESRCH;
    
    if (nsems > SEMMSL) {
        return -EINVAL;
    }
    
    spin_lock(&sem_lock);
    
    if (key != IPC_PRIVATE) {
        sem_set_t* existing = sem_find_by_key((int)key);
        if (existing) {
            if ((semflg & IPC_CREAT) && (semflg & IPC_EXCL)) {
                spin_unlock(&sem_lock);
                return -EEXIST;
            }
            int id = existing->id;
            spin_unlock(&sem_lock);
            return id;
        }
        
        if (!(semflg & IPC_CREAT)) {
            spin_unlock(&sem_lock);
            return -ENOENT;
        }
    }
    
    sem_set_t* set = sem_alloc_slot();
    if (!set) {
        spin_unlock(&sem_lock);
        return -ENOSPC;
    }
    
    set->key   = (int)key;
    set->nsems = (int)nsems;
    set->uid   = proc->euid;
    set->gid   = proc->egid;
    set->mode  = semflg & 0777;
    
    for (int i = 0; i < (int)nsems; i++) {
        set->sems[i].semval = 0;
        set->sems[i].sempid = 0;
    }
    
    int id = set->id;
    spin_unlock(&sem_lock);
    
    return id;
}

int32_t sys_semop(uint32_t semid, uint32_t sops_ptr, uint32_t nsops, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    if (!sem_initialized) return -EINVAL;
    if (nsops == 0 || nsops > 32) return -EINVAL;
    
    process_t* proc = get_current_process();
    if (!proc) return -ESRCH;
    
    sembuf_t ops[32];
    extern int copy_from_user(void*, const void*, size_t);
    if (copy_from_user(ops, (void*)sops_ptr, nsops * sizeof(sembuf_t)) < 0) {
        return -EFAULT;
    }
    
    spin_lock(&sem_lock);
    
    sem_set_t* set = sem_find_by_id((int)semid);
    if (!set) {
        spin_unlock(&sem_lock);
        return -EINVAL;
    }
    
    for (size_t i = 0; i < nsops; i++) {
        if (ops[i].sem_num >= (uint16_t)set->nsems) {
            spin_unlock(&sem_lock);
            return -EFBIG;
        }
    }
    
retry:
    for (size_t i = 0; i < nsops; i++) {
        sem_t* sem = &set->sems[ops[i].sem_num];
        int16_t op = ops[i].sem_op;
        
        if (op > 0) {
            /* Increase semaphore value - never blocks */
            sem->semval += op;
        } else if (op < 0) {
            /* Decrease semaphore value */
            if (sem->semval + op < 0) {
                /* Would block - check IPC_NOWAIT flag */
                if (ops[i].sem_flg & IPC_NOWAIT) {
                    spin_unlock(&sem_lock);
                    return -EAGAIN;
                }
                /* Block and retry */
                spin_unlock(&sem_lock);
                proc->state = PROCESS_BLOCKED;
                extern void schedule(void);
                schedule();
                /* Check for signals */
                if (proc->signal_pending) {
                    return -EINTR;
                }
                spin_lock(&sem_lock);
                /* Re-validate set still exists */
                set = sem_find_by_id((int)semid);
                if (!set) {
                    spin_unlock(&sem_lock);
                    return -EIDRM;
                }
                goto retry;
            }
            sem->semval += op;
        } else {
            /* Wait for zero */
            if (sem->semval != 0) {
                if (ops[i].sem_flg & IPC_NOWAIT) {
                    spin_unlock(&sem_lock);
                    return -EAGAIN;
                }
                spin_unlock(&sem_lock);
                proc->state = PROCESS_BLOCKED;
                extern void schedule(void);
                schedule();
                if (proc->signal_pending) {
                    return -EINTR;
                }
                spin_lock(&sem_lock);
                set = sem_find_by_id((int)semid);
                if (!set) {
                    spin_unlock(&sem_lock);
                    return -EIDRM;
                }
                goto retry;
            }
        }
        sem->sempid = proc->pid;
    }
    
    spin_unlock(&sem_lock);
    return 0;
}

int32_t sys_semctl(uint32_t semid, uint32_t semnum, uint32_t cmd, uint32_t arg, uint32_t u5) {
    (void)u5;
    
    if (!sem_initialized) return -EINVAL;
    
    spin_lock(&sem_lock);
    
    sem_set_t* set = sem_find_by_id((int)semid);
    if (!set) {
        spin_unlock(&sem_lock);
        return -EINVAL;
    }
    
    int result = 0;
    
    switch (cmd) {
        case IPC_RMID:
            /* Remove semaphore set */
            set->in_use = 0;
            break;
            
        case SETVAL:
            /* Set semaphore value */
            if (semnum >= (uint32_t)set->nsems) {
                spin_unlock(&sem_lock);
                return -EINVAL;
            }
            set->sems[semnum].semval = (int16_t)arg;
            break;
            
        case GETVAL:
            /* Get semaphore value */
            if (semnum >= (uint32_t)set->nsems) {
                spin_unlock(&sem_lock);
                return -EINVAL;
            }
            result = set->sems[semnum].semval;
            break;
            
        default:
            spin_unlock(&sem_lock);
            return -EINVAL;
    }
    
    spin_unlock(&sem_lock);
    return result;
}
