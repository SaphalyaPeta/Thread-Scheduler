#include "api.h"
#include "scheduler.h"
#include <stdbool.h>
#include <stdio.h>

// Interface implementation
// Implement APIs here...

int cpu_me(float current_time, int tid, int remaining_time) {
    pthread_mutex_lock(&scheduler_mutex);
    printf("CPU called for tid:%d called at time: %f\n", tid, current_time);

    thread_control_block_t* tcb = &tcb_array[tid];
    tcb->remaining_time = remaining_time;
    
    // CPU burst ended for the thread.
    if (remaining_time == 0) {
        int int_time = ceil(current_time);
        
        if (scheduler_type == SCH_FCFS || scheduler_type == SCH_SRTF) {
            dequeue_tid_from_q(&ready_queue, tid);
        } else {
            dequeue_tid_from_q(&mlfq[mlfq_data[tid].level], tid);
        }

        pthread_mutex_unlock(&scheduler_mutex);
        return int_time;
    }

    if (scheduler_type == SCH_FCFS) {
        // A new burst happens at start or afterwards.
        bool new_burst = (tcb->last_cpu_remaining <= 0 || remaining_time > tcb->last_cpu_remaining);
        if (new_burst) {
            tcb->ready_arrival_tick = current_time;
            if (current_cpu_thread == tcb) {
                current_cpu_thread = NULL;
            }
        }
    } else if (scheduler_type == SCH_SRTF) {
        tcb->ready_arrival_tick = current_time;
        current_cpu_thread = NULL;
    } else {
        // MLFQ handling
        bool new_burst = (tcb->last_cpu_remaining <= 0 || remaining_time > tcb->last_cpu_remaining);
        if (new_burst) {
            printf("new burst detected in cpu me for tid: %d\n", tid);
            tcb->ready_arrival_tick = current_time;
            promote_on_new_burst(tcb);
            current_cpu_thread = NULL;
        }
    }

    // If this thread isn’t already running on CPU put it in the ready queue. 
    if (scheduler_type == SCH_FCFS || scheduler_type == SCH_SRTF) {
        if (current_cpu_thread != tcb) {
            printf("Enqueued for %d in CPU me\n", tcb->tid);
            enqueue(&ready_queue, tcb);
            tcb->state = STATE_READY;
        }
    }

    barrier_wait();

    // Checking if CPU is idle,
    printf("Barrier crossed for %d in cpu_me\n", tcb->tid);
    if (current_cpu_thread == NULL) {
        current_cpu_thread = select_next_thread();
        if (current_cpu_thread != NULL) {
            printf("current_cpu_thread is allocated to %d in cpu_me\n", current_cpu_thread->tid);
            current_cpu_thread->state = STATE_RUNNING;
            pthread_cond_signal(&current_cpu_thread->cond);
        }
    }
    
    // If this thread wasn’t chosen, it sleeps until the scheduler signals it.
    // Guarantees only one thread runs at a time.
    if (current_cpu_thread != tcb) {
        printf("%d thread is waiting in Cpu_me for signal\n", tcb->tid);
        // printf("Current CPU thread at this moment: %d\n",current_cpu_thread->tid);
        pthread_cond_wait(&tcb->cond, &scheduler_mutex);
    }
    printf("%d thread got the signal in cpu_me\n", tid);

    int int_time = ceil(current_time);
    advance_time_to(int_time);
    
    int return_time = global_time + 1;
    advance_time_to(return_time);
    if (tcb->remaining_time > 0) tcb->remaining_time--;
    tcb->last_cpu_remaining = tcb->remaining_time;

    if (scheduler_type == SCH_SRTF) {
        dequeue_tid_from_q(&ready_queue, tid);
    } else if (scheduler_type == SCH_MLFQ) {
        int tid = tcb->tid;
        mlfq_data[tid].quantum_used++;
    
        int lvl = mlfq_data[tid].level;
        int quantum = MLFQ_TIME_QUANTUM[lvl];
    
        // If thread used full quantum (and still not done), demote
        if (mlfq_data[tid].quantum_used >= quantum && tcb->remaining_time > 0) {
            dequeue_tid_from_q(&mlfq[lvl], tid);
            tcb->ready_arrival_tick = current_time;
            demote_mlfq_thread(tcb);
            if (current_cpu_thread == tcb) current_cpu_thread = NULL;
        }
    }
    
    arrived_count--;
    pthread_mutex_unlock(&scheduler_mutex);
    
    return return_time;
}

int io_me(float current_time, int tid, int duration) {
    pthread_mutex_lock(&scheduler_mutex);
    printf("io_me called for tid:%d\n", tid); 
    
    barrier_wait();
    
    printf("Barrier crossed for io_me %d\n", tid);
    thread_control_block_t* tcb = &tcb_array[tid];
    
    // If the current thread was on the CPU, it must give it up before blocking for I/O.
    if (current_cpu_thread == tcb) {
        current_cpu_thread = select_next_thread();
        if (current_cpu_thread != NULL) {
            printf("current_cpu_thread is allocated to %d in io_me\n", current_cpu_thread->tid);
            current_cpu_thread->state = STATE_RUNNING;
            pthread_cond_signal(&current_cpu_thread->cond);
            printf("%d signaled from io_me to get unblocked\n", current_cpu_thread->tid);
        }
    }
    
    // enqueue and wait for your turn
    enqueue(&io_queue, tcb);
    tcb->state = STATE_BLOCKED_IO;

    // If device is idle and this thread is at head, start it
    if (current_io_thread == NULL && peek(&io_queue) == tcb) {
        current_io_thread = tcb;
        pthread_cond_signal(&tcb->cond);
    }

    // Wait until we become the active IO thread
    while (current_io_thread != tcb) {
        pthread_cond_wait(&tcb->cond, &scheduler_mutex);
        printf("%d is waiting\n",tcb->tid);
    }

    int int_time = ceil(current_time);
    // advance_time_to(int_time);

    // Perform IO for full duration
    int start_time = 0;
    if (global_IO_time > int_time) {
        start_time = global_IO_time;
    } else {
        start_time = int_time;
    } 
    int io_completion_time = start_time + duration;
    advance_IO_time_to(io_completion_time);

    // IO complete: pop ourselves from queue and hand cpu to next in the waiting list
    (void)dequeue(&io_queue); // remove self (at head)
    current_io_thread = peek(&io_queue);
    if (current_io_thread) {
        pthread_cond_signal(&current_io_thread->cond);
    }

    arrived_count--;

    pthread_mutex_unlock(&scheduler_mutex);
    
    return io_completion_time;
}

int P(float current_time, int tid, int sem_id) {
    pthread_mutex_lock(&scheduler_mutex);

    printf("P called for tid:%d\n", tid);

    thread_control_block_t* tcb = &tcb_array[tid];
    barrier_wait();
    printf("barrier crossed for tid: %d\n", tid);
    
    int int_time = ceil(current_time);
    
    
    // Lock the semaphore's internal varibales
    pthread_mutex_lock(&semaphores[sem_id].mutex);
    
    // semaphore available
    if (semaphores[sem_id].value > 0) {
        semaphores[sem_id].value--;
        advance_time_to(int_time);
        pthread_mutex_unlock(&semaphores[sem_id].mutex);
        pthread_mutex_unlock(&scheduler_mutex);
        // P returns instantly at call’s integer tick
        return int_time;
    } else {
        printf("Sempahore not found for tid: %d in P\n", tid);
        // Semaphore unavailable therefore blocking
        // If the thread calling P was running on CPU, it must release CPU.
        // Scheduler immediately picks another ready thread.
        if (current_cpu_thread == tcb) {
            current_cpu_thread = select_next_thread();
            if (current_cpu_thread != NULL) {
                current_cpu_thread->state = STATE_RUNNING;
                pthread_cond_signal(&current_cpu_thread->cond);
                printf("%d Signaled from P after semaphore is not found\n", current_cpu_thread->tid);
            }
        }
        
        // Block on semaphore
        // Add this thread to the semaphore’s waiting list.
        tcb->state = STATE_BLOCKED_SEM;
        semaphores[sem_id].blocked_threads[semaphores[sem_id].blocked_count++] = tcb;
        pthread_mutex_unlock(&semaphores[sem_id].mutex);
        
        // Wait to be woken up
        while (tcb->state == STATE_BLOCKED_SEM) {
            printf("%d is waiting in P\n",tcb->tid);
            blocked_on_p_count++;
            pthread_cond_signal(&timeCond);
            pthread_cond_wait(&tcb->cond, &scheduler_mutex);
            blocked_on_p_count--;
            arrived_count++;
        }
        advance_time_to(int_time);
    }
    
    // Blocked case: we were woken by V() at an integer time; return that tick
    int ret = tcb->wake_time;
    arrived_count--;
    pthread_mutex_unlock(&scheduler_mutex);
    return ret;
}

int V(float current_time, int tid, int sem_id) {
    pthread_mutex_lock(&scheduler_mutex);
    printf("V called for tid:%d\n", tid);

    barrier_wait();
    printf("Barrier passed for %d in V\n",tid);

    int int_time = ceil(current_time);
    while (active_threads-blocked_on_p_count!=1 && global_time < int_time) {
        pthread_cond_wait(&timeCond, &scheduler_mutex);
    }

    thread_control_block_t* tcb = &tcb_array[tid];
    printf("V got a signal to run thread: %d\n", tid);

    pthread_mutex_lock(&semaphores[sem_id].mutex);
    
    if (semaphores[sem_id].blocked_count > 0) {
        // Find thread with lowest tid to wake up
        int min_tid = INT_MAX;
        int min_index = -1;
        
        for (int i = 0; i < semaphores[sem_id].blocked_count; i++) {
            if (semaphores[sem_id].blocked_threads[i]->tid < min_tid) {
                min_tid = semaphores[sem_id].blocked_threads[i]->tid;
                min_index = i;
            }
        }
        
        if (min_index != -1) {
            thread_control_block_t* tcb_to_wake = semaphores[sem_id].blocked_threads[min_index];
            
            // Remove from blocked list
            for (int i = min_index; i < semaphores[sem_id].blocked_count - 1; i++) {
                semaphores[sem_id].blocked_threads[i] = semaphores[sem_id].blocked_threads[i + 1];
            }
            semaphores[sem_id].blocked_count--;
            
            tcb_to_wake->wake_time = int_time;
            tcb_to_wake->state = STATE_READY;
            printf("%d signaled to be get unblocked after increasing semaphore in V\n", tcb_to_wake->tid);
            pthread_cond_signal(&tcb_to_wake->cond);

            // Reducing thread count so that main.c do not call any other function 
            // and it shouldn't find arrived_count == active_threads.
            // If it finds arrived_count == active_threads then the barrier might get crossed.
            // We do not want that rather after V, P should get completed and then another function should get executed.
            arrived_count--;
        }
    } else {
        semaphores[sem_id].value++;
    }
    
    arrived_count--;
    pthread_mutex_unlock(&semaphores[sem_id].mutex);
    pthread_mutex_unlock(&scheduler_mutex);
    
    return int_time;
}

void end_me(int tid) {
    pthread_mutex_lock(&scheduler_mutex);

    printf("end me called for tid:%d\n", tid);
    
    thread_control_block_t* tcb = &tcb_array[tid];
    tcb->state = STATE_TERMINATED;
    
    // printf("In end me current thread: %d and tcb: %d", current_cpu_thread->tcb)
    // If this was the current CPU thread, select a new one
    if (current_cpu_thread == tcb) {
        printf("Selecting next thread from here\n");
        current_cpu_thread = select_next_thread();
        if (current_cpu_thread != NULL) {
            current_cpu_thread->state = STATE_RUNNING;
            pthread_cond_signal(&current_cpu_thread->cond);
            printf("%d signaled from endme\n",current_cpu_thread->tid);
        }
    }

    active_threads--;

    // After active threads are reduced, need to wake up any thread which might be waiting for barrier cond.
    pthread_cond_broadcast(&barrier_cond);
    pthread_cond_broadcast(&timeCond);
    pthread_mutex_unlock(&scheduler_mutex);
}