#ifndef SCHED_PROCESS_H
#define SCHED_PROCESS_H

#include <kernel/types.h>
#include <fs/vfs.h>

typedef int32_t pid_t;
typedef int32_t tid_t;

#define MAX_PROCESS_FDS 32

typedef struct file_descriptor {
    vfs_node_t* node;
    uint32_t    offset;
    uint32_t    flags;
    bool        used;
} file_descriptor_t;

typedef enum {
    THREAD_EMBRYO,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_TERMINATED
} thread_state_t;

/* Callee-saved context preserved during switch_to */
typedef struct context {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebx;
    uint32_t ebp;
    uint32_t eip;
} context_t;

#define THREAD_STACK_SIZE   8192
#define THREAD_NAME_LEN     32
#define DEFAULT_TIME_SLICE  5   /* 5 ticks @ 100 Hz = 50 ms */

#define PRIORITY_MIN        0
#define PRIORITY_DEFAULT    16
#define PRIORITY_MAX        31
#define MLFQ_LEVELS         4

typedef struct thread {
    tid_t           tid;
    pid_t           pid;
    char            name[THREAD_NAME_LEN];
    thread_state_t  state;
    context_t*      context;          /* Saved kernel stack pointer */
    void*           stack_base;       /* Allocated kernel stack base for kfree */
    size_t          stack_size;
    void            (*entry)(void*);
    void*           arg;

    /* User Mode & Address Space */
    bool            is_user;          /* True if executing in Ring 3 */
    uint32_t        cr3;              /* Page directory physical address */
    uint32_t        user_entry;       /* User space virtual entry point */
    uint32_t        user_stack;       /* User space virtual stack pointer */

    /* Filesystem & Per-Process File Descriptors */
    file_descriptor_t fd_table[MAX_PROCESS_FDS];
    char              cwd[VFS_PATH_MAX];

    /* Scheduling parameters */
    uint32_t        priority;         /* Base priority (0-31) */
    uint32_t        dynamic_priority; /* Effective priority with aging */
    uint32_t        mlfq_level;       /* Current MLFQ queue level (0-3) */
    uint32_t        time_slice;       /* Remaining quantum in current slice */
    uint32_t        time_quantum;     /* Total quantum allocated */

    /* Metrics & Telemetry */
    uint32_t        ticks_consumed;   /* Total CPU execution ticks */
    uint32_t        wait_ticks;       /* Ticks spent waiting in READY state */
    uint32_t        switches_count;   /* Number of context switches */
    uint32_t        create_tick;      /* Timestamp of creation */
    uint32_t        first_run_tick;   /* Timestamp of first execution */
    uint32_t        response_time;    /* (first_run_tick - create_tick) */

    /* Queue Links */
    struct thread*  next;             /* Global thread list */
    struct thread*  sched_next;       /* Scheduling queue link */
    struct thread*  dead_next;        /* Reclamation list */
} thread_t;

/* Process Control Block (PCB) */
typedef struct process {
    pid_t           pid;
    char            name[THREAD_NAME_LEN];
    uint32_t        page_directory;   /* Physical address of CR3 */
    thread_t*       main_thread;
    struct process* next;
} process_t;

void      process_subsystem_init(void);
thread_t* thread_create(const char* name, void (*entry)(void*), void* arg);
thread_t* thread_create_priority(const char* name, void (*entry)(void*), void* arg, uint32_t priority);
thread_t* process_create_user(const char* name, const void* code, size_t code_size);
void      thread_set_priority(thread_t* thread, uint32_t priority);
uint32_t  thread_get_priority(thread_t* thread);
void      thread_exit(void);
void      thread_yield(void);
thread_t* thread_get_current(void);
size_t    thread_get_count(void);
size_t    process_get_active_count(void);
size_t    process_get_terminated_count(void);
thread_t* thread_get_all_list(void);
void      process_list(void);

/* Per-Process File Descriptors */
void               process_fd_init(thread_t* t);
int                process_fd_alloc(thread_t* t, vfs_node_t* node, uint32_t flags);
file_descriptor_t* process_fd_get(thread_t* t, int fd);
int                process_fd_free(thread_t* t, int fd);
void               process_fd_cleanup(thread_t* t);

#endif /* SCHED_PROCESS_H */
