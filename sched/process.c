#include <sched/process.h>
#include <sched/sched.h>
#include <arch/x86/pit.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/kheap.h>
#include <kernel/printk.h>

extern void enter_user_mode(uint32_t user_eip, uint32_t user_esp);

static tid_t next_tid = 1;
static pid_t next_pid = 1;
static thread_t* all_threads = NULL;
static size_t total_threads_count = 0;

static void copy_string(char* dest, const char* src, size_t max_len) {
    size_t i = 0;
    while (src && src[i] != '\0' && i < max_len - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

void process_subsystem_init(void) {
    all_threads = NULL;
    total_threads_count = 0;
    next_tid = 1;
    next_pid = 1;
}

thread_t* thread_create_priority(const char* name, void (*entry)(void*), void* arg, uint32_t priority) {
    if (!entry) return NULL;

    thread_t* thread = (thread_t*)kmalloc(sizeof(thread_t));
    if (!thread) return NULL;

    thread->stack_base = kmalloc(THREAD_STACK_SIZE);
    if (!thread->stack_base) {
        kfree(thread);
        return NULL;
    }
    thread->stack_size = THREAD_STACK_SIZE;

    thread->tid = next_tid++;
    thread->pid = 0; /* Kernel threads belong to PID 0 */
    copy_string(thread->name, name ? name : "kthread", THREAD_NAME_LEN);
    thread->state = THREAD_READY;
    thread->entry = entry;
    thread->arg = arg;

    thread->is_user = false;
    thread->cr3 = 0;
    thread->user_entry = 0;
    thread->user_stack = 0;

    if (priority > PRIORITY_MAX) priority = PRIORITY_MAX;
    thread->priority = priority;
    thread->dynamic_priority = priority;
    thread->mlfq_level = 0;
    thread->time_slice = DEFAULT_TIME_SLICE;
    thread->time_quantum = DEFAULT_TIME_SLICE;
    thread->ticks_consumed = 0;
    thread->wait_ticks = 0;
    thread->switches_count = 0;
    thread->create_tick = pit_get_ticks();
    thread->first_run_tick = 0;
    thread->response_time = 0;
    thread->next = NULL;
    thread->sched_next = NULL;
    thread->dead_next = NULL;

    /* Initialize per-process file descriptor table and cwd */
    process_fd_init(thread);

    /* Synthesize initial stack context for switch_to (16-byte aligned sp) */
    uint32_t* sp = (uint32_t*)(((uintptr_t)thread->stack_base + thread->stack_size) & ~15);

    *(--sp) = (uint32_t)arg;           /* [sp - 1]: Argument to entry */
    *(--sp) = (uint32_t)thread_exit;   /* [sp - 2]: Return address from entry -> thread_exit */
    *(--sp) = (uint32_t)entry;         /* [sp - 3]: EIP popped by switch_to ret */
    *(--sp) = 0x00000202;              /* [sp - 4]: EFLAGS (IF=1 enabled) */
    *(--sp) = 0;                       /* [sp - 5]: EBP */
    *(--sp) = 0;                       /* [sp - 6]: EBX */
    *(--sp) = 0;                       /* [sp - 7]: ESI */
    *(--sp) = 0;                       /* [sp - 8]: EDI */

    thread->context = (context_t*)sp;

    /* Add to all threads list */
    thread->next = all_threads;
    all_threads = thread;
    total_threads_count++;

    /* Enqueue into scheduler ready queue */
    scheduler_enqueue(thread);
    return thread;
}

thread_t* thread_create(const char* name, void (*entry)(void*), void* arg) {
    return thread_create_priority(name, entry, arg, PRIORITY_DEFAULT);
}

static void user_thread_trampoline(void* arg) {
    thread_t* curr = (thread_t*)arg;
    enter_user_mode(curr->user_entry, curr->user_stack);
}

thread_t* process_create_user(const char* name, const void* code, size_t code_size) {
    if (!code || code_size == 0) return NULL;

    /* 1. Create a dedicated isolated page directory for the process */
    uint32_t user_cr3 = vmm_create_address_space();
    if (!user_cr3) return NULL;

    /* 2. Allocate and map user code page (Read-Only + Executable for Ring 3) */
    void* code_frame = pmm_alloc_frame_low(0x03000000);
    if (!code_frame) {
        vmm_destroy_address_space(user_cr3);
        return NULL;
    }

    uint8_t* dst = (uint8_t*)code_frame;
    const uint8_t* src = (const uint8_t*)code;
    for (size_t i = 0; i < code_size && i < PAGE_SIZE; i++) {
        dst[i] = src[i];
    }

    if (vmm_map_user_page(user_cr3, USER_SPACE_START, (uint32_t)code_frame,
                          PAGE_PRESENT | PAGE_USER) != 0) {
        pmm_free_frame(code_frame);
        vmm_destroy_address_space(user_cr3);
        return NULL;
    }

    /* 3. Allocate and map private user data page (4 KB data at 0x40002000) */
    void* data_frame = pmm_alloc_frame_low(0x03000000);
    if (!data_frame) {
        vmm_destroy_address_space(user_cr3);
        return NULL;
    }

    if (vmm_map_user_page(user_cr3, 0x40002000, (uint32_t)data_frame,
                          PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) != 0) {
        pmm_free_frame(data_frame);
        vmm_destroy_address_space(user_cr3);
        return NULL;
    }

    /* 4. Allocate and map user stack page (4 KB stack at 0x40010000) */
    void* stack_frame = pmm_alloc_frame_low(0x03000000);
    if (!stack_frame) {
        vmm_destroy_address_space(user_cr3);
        return NULL;
    }

    if (vmm_map_user_page(user_cr3, 0x40010000, (uint32_t)stack_frame,
                          PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) != 0) {
        pmm_free_frame(stack_frame);
        vmm_destroy_address_space(user_cr3);
        return NULL;
    }

    uint32_t user_stack_top = (0x40010000 + 4096) & ~15;

    /* 4. Allocate Thread Control Block */
    thread_t* thread = (thread_t*)kmalloc(sizeof(thread_t));
    if (!thread) {
        vmm_destroy_address_space(user_cr3);
        return NULL;
    }

    thread->stack_base = kmalloc(THREAD_STACK_SIZE);
    if (!thread->stack_base) {
        kfree(thread);
        vmm_destroy_address_space(user_cr3);
        return NULL;
    }
    thread->stack_size = THREAD_STACK_SIZE;

    thread->tid = next_tid++;
    thread->pid = next_pid++;
    copy_string(thread->name, name ? name : "uproc", THREAD_NAME_LEN);
    thread->state = THREAD_READY;
    thread->entry = (void (*)(void*))user_thread_trampoline;
    thread->arg = thread;

    thread->is_user = true;
    thread->cr3 = user_cr3;
    thread->user_entry = USER_SPACE_START;
    thread->user_stack = user_stack_top;

    thread->priority = PRIORITY_DEFAULT;
    thread->dynamic_priority = PRIORITY_DEFAULT;
    thread->mlfq_level = 0;
    thread->time_slice = DEFAULT_TIME_SLICE;
    thread->time_quantum = DEFAULT_TIME_SLICE;
    thread->ticks_consumed = 0;
    thread->wait_ticks = 0;
    thread->switches_count = 0;
    thread->create_tick = pit_get_ticks();
    thread->first_run_tick = 0;
    thread->response_time = 0;
    thread->next = NULL;
    thread->sched_next = NULL;
    thread->dead_next = NULL;

    /* Initialize per-process file descriptor table and cwd */
    process_fd_init(thread);

    /* Synthesize initial stack context to enter trampoline */
    uint32_t* sp = (uint32_t*)(((uintptr_t)thread->stack_base + thread->stack_size) & ~15);

    *(--sp) = (uint32_t)thread;                  /* Arg to user_thread_trampoline */
    *(--sp) = (uint32_t)thread_exit;             /* Return address */
    *(--sp) = (uint32_t)user_thread_trampoline;  /* EIP */
    *(--sp) = 0x00000202;                        /* EFLAGS (IF=1) */
    *(--sp) = 0;                                 /* EBP */
    *(--sp) = 0;                                 /* EBX */
    *(--sp) = 0;                                 /* ESI */
    *(--sp) = 0;                                 /* EDI */

    thread->context = (context_t*)sp;

    /* Add to all threads list */
    thread->next = all_threads;
    all_threads = thread;
    total_threads_count++;

    scheduler_enqueue(thread);
    return thread;
}

void thread_set_priority(thread_t* thread, uint32_t priority) {
    if (!thread) return;
    if (priority > PRIORITY_MAX) priority = PRIORITY_MAX;
    thread->priority = priority;
    thread->dynamic_priority = priority;
}

uint32_t thread_get_priority(thread_t* thread) {
    return thread ? thread->priority : 0;
}

thread_t* thread_get_all_list(void) {
    return all_threads;
}

void thread_exit(void) {
    thread_t* curr = thread_get_current();
    if (curr) {
        curr->state = THREAD_TERMINATED;
    }
    thread_yield();

    /* Should never reach here */
    while (1) {
        __asm__ volatile ("hlt");
    }
}

void thread_yield(void) {
    scheduler_yield();
}

size_t thread_get_count(void) {
    return total_threads_count;
}

size_t process_get_active_count(void) {
    size_t count = 0;
    thread_t* curr = all_threads;
    while (curr != NULL) {
        if (curr->state == THREAD_READY || curr->state == THREAD_RUNNING || curr->state == THREAD_BLOCKED) {
            count++;
        }
        curr = curr->next;
    }
    return count;
}

size_t process_get_terminated_count(void) {
    size_t count = 0;
    thread_t* curr = all_threads;
    while (curr != NULL) {
        if (curr->state == THREAD_TERMINATED) {
            count++;
        }
        curr = curr->next;
    }
    return count;
}

static const char* state_to_str(thread_state_t state) {
    switch (state) {
        case THREAD_READY:      return "READY";
        case THREAD_RUNNING:    return "RUNNING";
        case THREAD_BLOCKED:    return "BLOCKED";
        case THREAD_TERMINATED: return "TERMINATED";
        default:                return "UNKNOWN";
    }
}

void process_list(void) {
    printk("\nTID   PID  NAME             MODE   STATE        PRIO DYN MLFQ TICKS   WAIT   SWITCHES\n");
    printk("--------------------------------------------------------------------------------------\n");

    thread_t* curr = all_threads;
    while (curr != NULL) {
        /* TID */
        printk("%d", curr->tid);
        if (curr->tid < 10) printk("     ");
        else if (curr->tid < 100) printk("    ");
        else printk("   ");

        /* PID */
        printk("%d", curr->pid);
        if (curr->pid < 10) printk("    ");
        else if (curr->pid < 100) printk("   ");
        else printk("  ");

        /* NAME */
        printk("%s", curr->name);
        size_t nlen = 0;
        while (curr->name[nlen]) nlen++;
        while (nlen++ < 17) printk(" ");

        /* MODE */
        if (curr->is_user) {
            printk("Ring 3 ");
        } else {
            printk("Ring 0 ");
        }

        /* STATE */
        const char* st = state_to_str(curr->state);
        printk("%s", st);
        size_t slen = 0;
        while (st[slen]) slen++;
        while (slen++ < 13) printk(" ");

        /* PRIO, DYN, MLFQ */
        printk("%u", curr->priority);
        if (curr->priority < 10) printk("    ");
        else printk("   ");

        printk("%u", curr->dynamic_priority);
        if (curr->dynamic_priority < 10) printk("   ");
        else printk("  ");

        printk("Q%u   ", curr->mlfq_level);

        /* TICKS, WAIT, SWITCHES */
        printk("%u", curr->ticks_consumed);
        if (curr->ticks_consumed < 10) printk("       ");
        else if (curr->ticks_consumed < 100) printk("      ");
        else if (curr->ticks_consumed < 1000) printk("     ");
        else printk("    ");

        printk("%u", curr->wait_ticks);
        if (curr->wait_ticks < 10) printk("      ");
        else if (curr->wait_ticks < 100) printk("     ");
        else if (curr->wait_ticks < 1000) printk("    ");
        else printk("   ");

        printk("%u\n", curr->switches_count);

        curr = curr->next;
    }
    printk("--------------------------------------------------------------------------------------\n");
}

/* ========================================================================= */
/* Per-Process File Descriptors Implementation                               */
/* ========================================================================= */
void process_fd_init(thread_t* t) {
    if (!t) return;

    /* Initialize CWD to root "/" */
    t->cwd[0] = '/';
    t->cwd[1] = '\0';

    /* Standard Streams: 0 (stdin), 1 (stdout), 2 (stderr) */
    t->fd_table[0].node = NULL;
    t->fd_table[0].offset = 0;
    t->fd_table[0].flags = VFS_O_RDONLY;
    t->fd_table[0].used = true;

    t->fd_table[1].node = NULL;
    t->fd_table[1].offset = 0;
    t->fd_table[1].flags = VFS_O_WRONLY;
    t->fd_table[1].used = true;

    t->fd_table[2].node = NULL;
    t->fd_table[2].offset = 0;
    t->fd_table[2].flags = VFS_O_WRONLY;
    t->fd_table[2].used = true;

    for (int i = 3; i < MAX_PROCESS_FDS; i++) {
        t->fd_table[i].node = NULL;
        t->fd_table[i].offset = 0;
        t->fd_table[i].flags = 0;
        t->fd_table[i].used = false;
    }
}

int process_fd_alloc(thread_t* t, vfs_node_t* node, uint32_t flags) {
    if (!t || !node) return -1;

    for (int i = 3; i < MAX_PROCESS_FDS; i++) {
        if (!t->fd_table[i].used) {
            t->fd_table[i].node = node;
            t->fd_table[i].offset = (flags & VFS_O_APPEND) ? node->size : 0;
            t->fd_table[i].flags = flags;
            t->fd_table[i].used = true;
            return i;
        }
    }
    return -1; /* Out of file descriptors */
}

file_descriptor_t* process_fd_get(thread_t* t, int fd) {
    if (!t || fd < 0 || fd >= MAX_PROCESS_FDS) return NULL;
    if (!t->fd_table[fd].used) return NULL;
    return &t->fd_table[fd];
}

int process_fd_free(thread_t* t, int fd) {
    if (!t || fd < 3 || fd >= MAX_PROCESS_FDS) return -1;
    if (!t->fd_table[fd].used) return -1;

    if (t->fd_table[fd].node) {
        vfs_close(t->fd_table[fd].node);
    }
    t->fd_table[fd].node = NULL;
    t->fd_table[fd].offset = 0;
    t->fd_table[fd].flags = 0;
    t->fd_table[fd].used = false;
    return 0;
}

void process_fd_cleanup(thread_t* t) {
    if (!t) return;
    for (int i = 3; i < MAX_PROCESS_FDS; i++) {
        if (t->fd_table[i].used) {
            process_fd_free(t, i);
        }
    }
}
