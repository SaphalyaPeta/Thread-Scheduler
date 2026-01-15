#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <float.h>
#include <pthread.h>

#include "api.h"

#define MAX_THREADS 128

// Thread states
typedef enum {
    STATE_READY,
    STATE_RUNNING,
    STATE_BLOCKED_IO,
    STATE_BLOCKED_SEM,
    STATE_TERMINATED
} thread_state_t;

// Thread Control Block
typedef struct {
    int tid;
    float arrival_time;
    int remaining_time;
    float ready_arrival_tick;
    int last_cpu_remaining;
    int wake_time;
    thread_state_t state;
    pthread_cond_t cond;
} thread_control_block_t;

typedef struct {
    int level;          // current MLFQ level (0 = highest)
    int quantum_used;   // ticks used in current level
} mlfq_info_t;

// Semaphore structure
typedef struct {
    int value;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    thread_control_block_t* blocked_threads[MAX_THREADS];
    int blocked_count;
} semaphore_t;

// Queue structure
typedef struct {
    thread_control_block_t* threads[MAX_THREADS];
    int front;
    int rear;
    int count;
} queue_t;

// Global variables
extern int global_time;
extern int global_IO_time;
extern enum sch_type scheduler_type;
extern int thread_count;
extern pthread_mutex_t scheduler_mutex;

extern int active_threads;
extern int arrived_count;    // number of threads still alive (not terminated)
extern pthread_cond_t barrier_cond;
extern pthread_cond_t timeCond;
extern int blocked_on_p_count;

extern thread_control_block_t* tcb_array;
extern queue_t ready_queue;
extern queue_t io_queue;
extern semaphore_t semaphores[MAX_NUM_SEM];
extern queue_t mlfq[5];
extern mlfq_info_t mlfq_data[MAX_THREADS];

extern thread_control_block_t* current_cpu_thread;
extern thread_control_block_t* current_io_thread;

// Functions
void init_queue(queue_t* q);
void enqueue(queue_t* q, thread_control_block_t* tcb);
thread_control_block_t* dequeue(queue_t* q);
thread_control_block_t* dequeue_at_index(queue_t* q, int absolute_index);
void dequeue_tid_from_q(queue_t* q, int tid);
thread_control_block_t* peek(queue_t* q);
void advance_time_to(int target_time);
void advance_IO_time_to(int target_time);
thread_control_block_t* select_next_thread();
thread_control_block_t* select_next_thread_fcfs(queue_t* q);
thread_control_block_t* select_next_thread_srtf();
thread_control_block_t* select_next_thread_mlfq();
void enqueue_mlfq(thread_control_block_t* tcb, int level);
void demote_mlfq_thread(thread_control_block_t* tcb);
void promote_on_new_burst(thread_control_block_t* tcb);
void barrier_wait();