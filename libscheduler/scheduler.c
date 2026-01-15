#include "scheduler.h"
#include "api.h"
#include <stdio.h>

// Global variables definition
int global_time = 0;
int global_IO_time = 0;
enum sch_type scheduler_type;
int thread_count;
int active_threads;
int arrived_count = 0;
int blocked_on_p_count = 0;
queue_t mlfq[5];
mlfq_info_t mlfq_data[MAX_THREADS];

pthread_cond_t barrier_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t timeCond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t scheduler_mutex = PTHREAD_MUTEX_INITIALIZER;

thread_control_block_t* tcb_array = NULL;
queue_t ready_queue;
queue_t io_queue;
semaphore_t semaphores[MAX_NUM_SEM];

thread_control_block_t* current_cpu_thread = NULL;
thread_control_block_t* current_io_thread = NULL;

// Queue operations
void init_queue(queue_t* q) {
    q->front = 0;
    q->rear = -1;
    q->count = 0;
}

void enqueue(queue_t* q, thread_control_block_t* tcb) {
    if (q->count < MAX_THREADS) {
        q->rear = (q->rear + 1) % MAX_THREADS;
        q->threads[q->rear] = tcb;
        q->count++;
    }
}

thread_control_block_t* dequeue(queue_t* q) {
    if (q->count == 0) return NULL;
    
    thread_control_block_t* tcb = q->threads[q->front];
    q->front = (q->front + 1) % MAX_THREADS;
    q->count--;
    return tcb;
}

thread_control_block_t* peek(queue_t* q) {
    if (q->count == 0) return NULL;
    return q->threads[q->front];
}

thread_control_block_t* dequeue_at_index(queue_t* q, int absolute_index) {
    if (q->count == 0) return NULL;
    int idx = absolute_index % MAX_THREADS;
    thread_control_block_t* res = q->threads[idx];
    int last = (q->front + q->count - 1) % MAX_THREADS;
    while (idx != last) {
        int next = (idx + 1) % MAX_THREADS;
        q->threads[idx] = q->threads[next];
        idx = next;
    }
    q->rear = (q->rear - 1 + MAX_THREADS) % MAX_THREADS;
    q->count--;
    return res;
}

void dequeue_tid_from_q(queue_t* q, int tid) {
    int idx = -1;
    for (int i = 0; i < q->count; i++) {
        int pos = (q->front + i) % MAX_THREADS;
        if (q->threads[pos]->tid == tid) {
            idx = pos;
            break;
        }
    }

    if (idx != -1) {
        dequeue_at_index(q, idx);
    }
}

void advance_time_to(int target_time) {
    while (global_time < target_time) {
        global_time++;
        printf("Global time : %d\n", global_time);
        pthread_cond_broadcast(&timeCond);
    }
}

void advance_IO_time_to(int target_time) {
    while (global_IO_time < target_time) {
        global_IO_time++;
    }
}

thread_control_block_t* select_next_thread() {
    if (scheduler_type == SCH_FCFS) {
        return select_next_thread_fcfs(&ready_queue);
    } 
    else if (scheduler_type == SCH_SRTF) {
        return select_next_thread_srtf();
    } else if (scheduler_type == SCH_MLFQ)
        return select_next_thread_mlfq();
    else {
        printf("Error: Unknown scheduler type!\n");
        return NULL;
    }
}

thread_control_block_t* select_next_thread_fcfs(queue_t* q) {
    if (q->count == 0) return NULL;

    int best_idx = -1;
    float best_tick = FLT_MAX;
    int best_tid  = INT_MAX;
    for (int i = 0; i < q->count; i++) {
        int idx = (q->front + i) % MAX_THREADS;
        thread_control_block_t* t = q->threads[idx];
        printf("Thread %d ready arrival tick %f\n", t->tid, t->ready_arrival_tick);
        if (t->ready_arrival_tick < best_tick ||
            (t->ready_arrival_tick == best_tick && t->tid < best_tid)) {
            best_tick = t->ready_arrival_tick;
            best_tid  = t->tid;
            best_idx  = idx;
        }
    }
    if (best_idx != -1) {
        thread_control_block_t* res = q->threads[best_idx];
        return res;
    }
    return NULL;
}

thread_control_block_t* select_next_thread_srtf() {
    if (ready_queue.count == 0) return NULL;

    // Temporary candidate array
    thread_control_block_t* candidates[MAX_THREADS];
    int candidate_count = 0;

    // Step 1 & 2: filter threads that are ready to run now
    for (int i = 0; i < ready_queue.count; i++) {
        int idx = (ready_queue.front + i) % MAX_THREADS;
        thread_control_block_t* t = ready_queue.threads[idx];

        if (t->ready_arrival_tick <= global_time) {
            candidates[candidate_count++] = t;
            printf("Candidate T%d: ready_arrival_tick=%f, remaining=%d\n",
                   t->tid, t->ready_arrival_tick, t->remaining_time);
        }
        else {
            printf("Lagging Candidate T%d: ready_arrival_tick=%f, remaining=%d\n",
                t->tid, t->ready_arrival_tick, t->remaining_time);
        }
    }

    // Step 3: pick best among candidates
    if (candidate_count == 0) {
        // No thread is ready yet → return NULL
        // If all threads are lagging behind the global time then we should signal thread which arrived first.
        // Also increase global time to that point.
        thread_control_block_t* earliest_thread = select_next_thread_fcfs(&ready_queue);
        if (earliest_thread != NULL) {
            advance_time_to(earliest_thread->ready_arrival_tick);
            return earliest_thread;
        }
        return NULL;
    }

    int best_idx = -1;
    int best_remaining = INT_MAX;
    int best_tid = INT_MAX;

    for (int i = 0; i < candidate_count; i++) {
        thread_control_block_t* t = candidates[i];
        if (t->remaining_time < best_remaining ||
            (t->remaining_time == best_remaining && t->tid < best_tid)) {
            best_remaining = t->remaining_time;
            best_tid = t->tid;
            best_idx = i;
        }
    }

    thread_control_block_t* res = candidates[best_idx];
    printf("Selected T%d by SRTF remaining=%d\n", res->tid, res->remaining_time);

    return res;
}

void enqueue_mlfq(thread_control_block_t* tcb, int level) {
    if (level < 0) level = 0;
    if (level >= 5) level = 4;
    enqueue(&mlfq[level], tcb);
    mlfq_data[tcb->tid].level = level;
    tcb->state = STATE_READY;
}

void demote_mlfq_thread(thread_control_block_t* tcb) {
    int old = mlfq_data[tcb->tid].level;
    int new_level = (old < 4) ? old + 1 : old;
    enqueue_mlfq(tcb, new_level);
    mlfq_data[tcb->tid].quantum_used = 0;
    printf("tid%d demoted from tid%d to tid%d\n", tcb->tid, old, new_level);
}

void promote_on_new_burst(thread_control_block_t* tcb) {
    mlfq_data[tcb->tid].level = 0;
    mlfq_data[tcb->tid].quantum_used = 0;
    enqueue_mlfq(tcb, 0);
}

// void print_mlfq_state() {
//     for (int lvl = 0; lvl < 5; lvl++) {
//         printf("Level %d (quantum=%d): ", lvl, MLFQ_TIME_QUANTUM[lvl]);

//         if (mlfq[lvl].count == 0) {
//             printf("[empty]\n");
//             continue;
//         }

//         // Iterate circularly over queue
//         for (int i = 0; i < mlfq[lvl].count; i++) {
//             int idx = (mlfq[lvl].front + i) % MAX_THREADS;
//             thread_control_block_t* t = mlfq[lvl].threads[idx];
//             printf("T%d(quantum_used=%d)(ready_arrival=%f)", 
//                    t->tid, mlfq_data[t->tid].quantum_used, t->ready_arrival_tick);
//         }
//         printf("\n");
//     }
// }

thread_control_block_t* select_next_thread_mlfq() {
    queue_t temp[5];
    for (int i = 0; i < 5; i++) {
        init_queue(&temp[i]);
    }

    for (int lvl = 0; lvl < 5; lvl++) {
        for (int i = 0; i < mlfq[lvl].count; i++) {
            int idx = (mlfq[lvl].front + i) % MAX_THREADS;
            thread_control_block_t* t = mlfq[lvl].threads[idx];
            if (t->ready_arrival_tick <= global_time) {
                enqueue(&temp[lvl], t);
                printf("T%d from L%d added to temp queue (ready tick %.1f <= global %d)\n",
                       t->tid, lvl, t->ready_arrival_tick, global_time);
            }
        }
    }

    for (int lvl = 0; lvl < 5; lvl++) {
        if (temp[lvl].count > 0) {
            thread_control_block_t* next = select_next_thread_fcfs(&temp[lvl]);
            mlfq_data[next->tid].level = lvl;
            printf("Picked T%d from level %d (quantum %d)\n",
                   next->tid, lvl, MLFQ_TIME_QUANTUM[lvl]);
            return next;
        }
    }

    int earliest_tick = INT_MAX;
    thread_control_block_t* earliest_thread = NULL;
    for (int lvl = 0; lvl < 5; lvl++) {
        for (int i = 0; i < mlfq[lvl].count; i++) {
            int idx = (mlfq[lvl].front + i) % MAX_THREADS;
            thread_control_block_t* t = mlfq[lvl].threads[idx];
            if (ceil(t->ready_arrival_tick) < earliest_tick || (ceil(t->ready_arrival_tick) == earliest_tick && t->tid < earliest_thread->tid)) {
                earliest_tick = ceil(t->ready_arrival_tick);
                earliest_thread = t;
            }
        }
    }

    if (earliest_thread != NULL) {
        printf("No thread ready. Advancing time to earliest arrival - T%d, tick %f)\n",
               earliest_thread->tid, earliest_thread->ready_arrival_tick);
        advance_time_to(ceil(earliest_thread->ready_arrival_tick));
        return earliest_thread;
    }

    return NULL;
}

void barrier_wait() {
    arrived_count++;
    printf("Arrived thread Count: %d\n", arrived_count);
    if (arrived_count < active_threads) {
        pthread_cond_wait(&barrier_cond, &scheduler_mutex);
    } else {
        // Last thread to arrive resets counter and wakes all
        pthread_cond_broadcast(&barrier_cond);
    }
}
