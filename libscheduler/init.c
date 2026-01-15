#include "api.h"
#include "scheduler.h"
#include <stdbool.h>

void init_scheduler(enum sch_type type, int count) {
    pthread_mutex_lock(&scheduler_mutex);
    
    scheduler_type = type;
    thread_count = count;
    active_threads = count;
    arrived_count = 0;
    global_time = 0;
    global_IO_time = 0;
    blocked_on_p_count = 0;

    tcb_array = malloc(sizeof(thread_control_block_t) * thread_count);
    for (int i = 0; i < thread_count; i++) {
        tcb_array[i].tid = i;
        tcb_array[i].state = STATE_READY;
        tcb_array[i].last_cpu_remaining = -1;
        tcb_array[i].ready_arrival_tick = 0;
        tcb_array[i].wake_time = 0;
        pthread_cond_init(&tcb_array[i].cond, NULL);
    }    
    init_queue(&ready_queue);
    init_queue(&io_queue);

    // Initialize MLFQ queues
    for (int i = 0; i < 5; i++) {
        init_queue(&mlfq[i]);
    }

    // Initialize per-thread MLFQ metadata
    for (int i = 0; i < MAX_THREADS; i++) {
        mlfq_data[i].level = 0;
        mlfq_data[i].quantum_used = 0;
    }

    for (int i = 0; i < MAX_NUM_SEM; i++) {
        semaphores[i].value = 0;
        semaphores[i].blocked_count = 0;
        pthread_mutex_init(&semaphores[i].mutex, NULL);
        pthread_cond_init(&semaphores[i].cond, NULL);
    }
    
    current_cpu_thread = NULL;
    current_io_thread = NULL;
    
    pthread_mutex_unlock(&scheduler_mutex);
}

void finish_scheduler() {
    pthread_mutex_lock(&scheduler_mutex);
    
    for (int i = 0; i < thread_count; i++) {
        pthread_cond_destroy(&tcb_array[i].cond);
    }
    
    for (int i = 0; i < MAX_NUM_SEM; i++) {
        pthread_mutex_destroy(&semaphores[i].mutex);
        pthread_cond_destroy(&semaphores[i].cond);
    }
    
    free(tcb_array);
    tcb_array = NULL;
    
    pthread_mutex_unlock(&scheduler_mutex);
}