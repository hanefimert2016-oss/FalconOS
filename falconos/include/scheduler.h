/*
 * FalconOS Process Scheduler
 * Multi-threading and process scheduling
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stddef.h>

#define MAX_PROCESSES 512
#define MAX_THREADS_PER_PROCESS 64
#define DEFAULT_TIME_SLICE 10 // milliseconds

typedef enum {
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_READY,
    PROCESS_STATE_BLOCKED,
    PROCESS_STATE_TERMINATED
} process_state_t;

typedef enum {
    SCHEDULER_POLICY_FIFO,
    SCHEDULER_POLICY_RR,
    SCHEDULER_POLICY_PRIORITY,
    SCHEDULER_POLICY_CFS
} scheduler_policy_t;

typedef struct {
    uint64_t rsp;
    uint64_t rip;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rflags;
} thread_context_t;

typedef struct {
    uint32_t tid;
    uint32_t pid;
    thread_context_t context;
    void* stack;
    size_t stack_size;
    int is_running;
    int priority;
} thread_t;

typedef struct {
    uint32_t pid;
    char name[256];
    process_state_t state;
    thread_t* threads[MAX_THREADS_PER_PROCESS];
    int thread_count;
    void* memory_space;
    uint64_t start_time;
    uint64_t cpu_time;
    int priority;
} process_t;

// Scheduler initialization
int init_scheduler();

// Process management
process_t* create_process(const char* name);
int terminate_process(uint32_t pid);
process_t* get_process(uint32_t pid);

// Thread management
thread_t* create_thread(process_t* process, void (*entry)(void*), void* arg);
int terminate_thread(uint32_t tid);
void schedule_thread(thread_t* thread);

// Scheduling
void scheduler_tick();
void yield();
void sleep(uint64_t ms);

// Context switching
void switch_context(thread_context_t* old_ctx, thread_context_t* new_ctx);

#endif // SCHEDULER_H
