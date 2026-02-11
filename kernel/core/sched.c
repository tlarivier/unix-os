#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <kernel/process.h>
#include <kernel/kernel.h>
#include <kernel/drivers.h>
#include <kernel/errno.h>
#include <kernel/constants.h>
#include <kernel/scheduler.h>
#include <kernel/spinlock.h>

/* MLFQ */
#define NUM_QUEUES 4
#define BASE_QUANTUM 5
static const uint32_t queue_quantum[NUM_QUEUES] = {5, 10, 20, 40};  /* Increasing quantums */

static process_t* run_queues[NUM_QUEUES] = {NULL};
process_t* current_process = NULL;
static uint32_t scheduler_ticks  = 0;
static uint32_t context_switches = 0;
static uint32_t quantum_left     = BASE_QUANTUM;
static int scheduler_enabled = 0;
static int current_queue     = 0;
static volatile int need_resched = 0;

void scheduler_init(void) {
    for (int i = 0; i < NUM_QUEUES; i++) {
        run_queues[i] = NULL;
    }
    current_process   = NULL;
    scheduler_ticks   = 0;
    context_switches  = 0;
    quantum_left      = BASE_QUANTUM;
    scheduler_enabled = 0;
    current_queue     = 0;
}

void scheduler_enable(void) {
    scheduler_enabled = 1;
}

static void add_to_queue(process_t* proc, int queue) {
    if (queue < 0) queue = 0;
    if (queue >= NUM_QUEUES) queue = NUM_QUEUES - 1;
    proc->priority = queue;  /* Store current queue level */
    
    if (!run_queues[queue]) {
        run_queues[queue] = proc;
        proc->next = proc;
    } else {
        proc->next = run_queues[queue]->next;
        run_queues[queue]->next = proc;
    }
}

static void remove_from_queue(process_t* proc) {
    int queue = proc->priority;
    if (queue < 0 || queue >= NUM_QUEUES) return;
    
    process_t** head = &run_queues[queue];
    if (!*head) return;
    
    if (*head == proc && proc->next == proc) {
        *head = NULL;
    } else if (*head == proc) {
        process_t* tail = *head;
        while (tail->next != *head) tail = tail->next;
        *head = proc->next;
        tail->next = *head;
    } else {
        process_t* prev = *head;
        while (prev->next != proc && prev->next != *head) prev = prev->next;
        if (prev->next == proc) prev->next = proc->next;
    }
    proc->next = NULL;
}

void scheduler_add_process(process_t* proc) {
    if (!proc) return;
    if (proc->pid == 0) return;
    proc->state = PROCESS_READY;
    proc->time_slice = queue_quantum[0];
    add_to_queue(proc, 0);
}

void scheduler_remove_process(process_t* proc) {
    if (!proc) return;
    remove_from_queue(proc);
}

static process_t* find_next_process(int* out_queue) {
    for (int q = 0; q < NUM_QUEUES; q++) {
        if (run_queues[q]) {
            *out_queue = q;
            return run_queues[q];
        }
    }
    return NULL;
}

void schedule(void) {
    uint32_t flags = local_irq_save();
    
    scheduler_ticks++;
    
    int next_queue = 0;
    process_t* next = find_next_process(&next_queue);
    if (!next) {
        local_irq_restore(flags);
        return;
    }
    
    if (quantum_left > 0) quantum_left--;
    
    if (quantum_left == 0 || !current_process || current_process->state != PROCESS_RUNNING) {
        
        if (current_process && current_process->state == PROCESS_RUNNING && quantum_left == 0) {
            remove_from_queue(current_process);
            int new_queue = current_process->priority + 1;
            if (new_queue >= NUM_QUEUES) new_queue = NUM_QUEUES - 1;
            current_process->state = PROCESS_READY;
            add_to_queue(current_process, new_queue);
        }
        
        next = find_next_process(&next_queue);
        if (next && next != current_process) {
            process_t* old = current_process;
            current_process = next;
            current_queue   = next_queue;
            current_process->state = PROCESS_RUNNING;
            quantum_left = queue_quantum[next_queue];
            run_queues[next_queue] = run_queues[next_queue]->next;
            context_switches++;
            
            if (old) {
                context_switch(old, current_process);
                return;  /* Context restored by new process */
            }
        } else if (current_process) {
            quantum_left = queue_quantum[current_process->priority];
        }
    }
    
    local_irq_restore(flags);
}

void yield(void) {
    if (!scheduler_enabled) return;
    
    if (current_process) {
        current_process->state = PROCESS_READY;
        int q = current_process->priority;
        if (q >= 0 && q < NUM_QUEUES && run_queues[q]) {
            run_queues[q] = run_queues[q]->next;
        }
    }
    
    quantum_left = 0;
    schedule();
}

void block_process(void) {
    if (current_process) {
        current_process->state = PROCESS_BLOCKED;
        scheduler_remove_process(current_process);
        quantum_left = 0;
        schedule();
    }
}

void unblock_process(process_t* proc) {
    if (proc && proc->state == PROCESS_BLOCKED) {
        int boost_queue = proc->priority - 1;
        if (boost_queue < 0) boost_queue = 0;
        proc->state = PROCESS_READY;
        add_to_queue(proc, boost_queue);
    }
}

void show_scheduler_stats(void) {
    int counts[NUM_QUEUES] = {0};
    for (int q = 0; q < NUM_QUEUES; q++) {
        if (run_queues[q]) {
            process_t* p = run_queues[q];
            do { counts[q]++; p = p->next; } while (p != run_queues[q]);
        }
    }
    kprintf("MLFQ Scheduler: ticks=%u switches=%u queues=[%d,%d,%d,%d]\n", 
            scheduler_ticks, context_switches, counts[0], counts[1], counts[2], counts[3]);
}

void timer_tick(void) {
    if (!scheduler_enabled) return;
    
    scheduler_ticks++;
    if (quantum_left > 0) quantum_left--;
    
    if (quantum_left == 0) {
        need_resched = 1;
    }
}

int check_need_resched(void) {
    return need_resched;
}

void do_resched(void) {
    if (!need_resched || !scheduler_enabled) return;
    need_resched = 0;
    
    if (current_process && current_process->state == PROCESS_RUNNING) {
        current_process->state = PROCESS_READY;
        int q = current_process->priority;
        if (q >= 0 && q < NUM_QUEUES && run_queues[q]) {
            run_queues[q] = run_queues[q]->next;
        }
    }
    
    schedule();
}
