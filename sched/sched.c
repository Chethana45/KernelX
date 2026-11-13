#include <sched/sched.h>
#include <sched/process.h>
#include <arch/x86/gdt.h>
#include <arch/x86/pit.h>
#include <mm/vmm.h>
#include <mm/kheap.h>
#include <kernel/printk.h>

extern void switch_to(context_t** prev, context_t* next);

static thread_t* current_thread = NULL;
static thread_t* idle_thread = NULL;
static thread_t  main_thread_struct;

static bool scheduler_enabled = false;
static uint32_t total_switches = 0;
static uint32_t voluntary_switches = 0;
static uint32_t preemptive_switches = 0;

static sched_policy_t active_policy = SCHED_POLICY_RR;
static sched_ops_t* current_ops = &rr_ops;

static thread_t* dead_thread_list = NULL;

static void idle_entry(void* arg) {
    (void)arg;
    while (1) {
        __asm__ volatile ("hlt");
    }
}

void scheduler_enqueue(thread_t* thread) {
    if (!thread || thread == idle_thread) return;
    if (current_ops && current_ops->enqueue) {
        current_ops->enqueue(thread);
    }
}

static thread_t* scheduler_dequeue(void) {
    if (current_ops && current_ops->dequeue) {
        return current_ops->dequeue();
    }
    return NULL;
}

static void cleanup_dead_threads(void) {
    while (dead_thread_list != NULL) {
        thread_t* t = dead_thread_list;
        dead_thread_list = dead_thread_list->dead_next;

        if (t->cr3 != 0) {
            vmm_destroy_address_space(t->cr3);
            t->cr3 = 0;
        }

        process_fd_cleanup(t);

        if (t->stack_base) {
            kfree(t->stack_base);
            t->stack_base = NULL;
        }
    }
}

static void schedule(void) {
    if (!scheduler_enabled) return;

    thread_t* prev = current_thread;

    if (prev->state == THREAD_RUNNING) {
        prev->state = THREAD_READY;
        scheduler_enqueue(prev);
    } else if (prev->state == THREAD_TERMINATED) {
        prev->dead_next = dead_thread_list;
        dead_thread_list = prev;
    }

    thread_t* next = scheduler_dequeue();
    if (!next) {
        next = idle_thread;
    }

    next->state = THREAD_RUNNING;
    current_thread = next;
    total_switches++;
    next->switches_count++;

    /* Measure response time on first scheduling */
    if (next->first_run_tick == 0 && next != idle_thread) {
        next->first_run_tick = pit_get_ticks();
        if (next->first_run_tick >= next->create_tick) {
            next->response_time = next->first_run_tick - next->create_tick;
        } else {
            next->response_time = 0;
        }
    }

    /* Switch page directory if entering user space or restoring kernel space */
    if (next->cr3 != 0) {
        if (vmm_get_current_cr3() != next->cr3) {
            vmm_switch_address_space(next->cr3);
        }
    } else {
        if (vmm_get_current_cr3() != vmm_get_directory_addr()) {
            vmm_switch_address_space(0);
        }
    }

    /* Update TSS esp0 for Ring 3 privilege stack switches */
    if (next->stack_base) {
        tss_set_stack((uint32_t)next->stack_base + next->stack_size);
    }

    if (prev != next) {
        switch_to(&prev->context, next->context);
    }

    cleanup_dead_threads();
}

void scheduler_yield(void) {
    voluntary_switches++;
    if (current_ops && current_ops->yield) {
        current_ops->yield(current_thread);
    }
    schedule();
}

void scheduler_tick(void) {
    if (!scheduler_enabled || !current_thread) return;

    current_thread->ticks_consumed++;

    if (current_ops && current_ops->tick) {
        current_ops->tick(current_thread);
    }
}

void scheduler_preempt(void) {
    preemptive_switches++;
    schedule();
}

void scheduler_set_policy(sched_policy_t policy) {
    if (policy == active_policy && current_ops != NULL) return;

    sched_ops_t* new_ops = NULL;
    switch (policy) {
        case SCHED_POLICY_RR:
            new_ops = &rr_ops;
            break;
        case SCHED_POLICY_PRIORITY:
            new_ops = &priority_ops;
            break;
        case SCHED_POLICY_MLFQ:
            new_ops = &mlfq_ops;
            break;
        default:
            return;
    }

    /* Drain all runnable threads from current scheduler without loss */
    thread_t* drained = NULL;
    if (current_ops && current_ops->drain_all) {
        drained = current_ops->drain_all();
    }

    /* Activate new scheduler */
    current_ops = new_ops;
    active_policy = policy;
    if (current_ops->init) {
        current_ops->init();
    }

    /* Re-enqueue all drained threads into the new scheduling policy */
    while (drained != NULL) {
        thread_t* next = drained->sched_next;
        drained->sched_next = NULL;
        if (drained->state == THREAD_READY) {
            scheduler_enqueue(drained);
        }
        drained = next;
    }
}

sched_policy_t scheduler_get_policy(void) {
    return active_policy;
}

const char* scheduler_get_policy_name(void) {
    if (current_ops && current_ops->name) {
        return current_ops->name;
    }
    return "Unknown";
}

void scheduler_init(void) {
    scheduler_enabled = false;
    total_switches = 0;
    voluntary_switches = 0;
    preemptive_switches = 0;
    dead_thread_list = NULL;

    process_subsystem_init();

    /* Initialize main kernel execution thread */
    main_thread_struct.tid = 0;
    main_thread_struct.pid = 0;
    for (int i = 0; i < THREAD_NAME_LEN; i++) main_thread_struct.name[i] = 0;
    main_thread_struct.name[0] = 'm';
    main_thread_struct.name[1] = 'a';
    main_thread_struct.name[2] = 'i';
    main_thread_struct.name[3] = 'n';
    main_thread_struct.state = THREAD_RUNNING;
    main_thread_struct.context = NULL;
    main_thread_struct.stack_base = NULL;
    main_thread_struct.stack_size = 0;
    main_thread_struct.priority = PRIORITY_DEFAULT;
    main_thread_struct.dynamic_priority = PRIORITY_DEFAULT;
    main_thread_struct.mlfq_level = 0;
    main_thread_struct.time_slice = DEFAULT_TIME_SLICE;
    main_thread_struct.time_quantum = DEFAULT_TIME_SLICE;
    main_thread_struct.ticks_consumed = 0;
    main_thread_struct.wait_ticks = 0;
    main_thread_struct.switches_count = 0;
    main_thread_struct.create_tick = 0;
    main_thread_struct.first_run_tick = 0;
    main_thread_struct.response_time = 0;
    main_thread_struct.next = NULL;
    main_thread_struct.sched_next = NULL;
    main_thread_struct.dead_next = NULL;

    process_fd_init(&main_thread_struct);

    current_thread = &main_thread_struct;

    /* Initialize default scheduler algorithm (Round Robin) */
    scheduler_set_policy(SCHED_POLICY_RR);

    /* Create idle thread */
    idle_thread = thread_create("idle", idle_entry, NULL);
    if (idle_thread) {
        idle_thread->tid = 0;
    }
}

void scheduler_start(void) {
    scheduler_enabled = true;
}

bool scheduler_is_active(void) {
    return scheduler_enabled;
}

thread_t* thread_get_current(void) {
    return current_thread;
}

void scheduler_get_stats(uint32_t* total, uint32_t* voluntary, uint32_t* preempts) {
    if (total)     *total = total_switches;
    if (voluntary) *voluntary = voluntary_switches;
    if (preempts)  *preempts = preemptive_switches;
}

uint32_t scheduler_calculate_fairness(void) {
    /* Calculates Jain's Fairness Index * 1000 for active threads */
    thread_t* curr = thread_get_all_list();
    uint64_t sum = 0;
    uint64_t sum_sq = 0;
    uint32_t n = 0;

    while (curr != NULL) {
        if (curr != idle_thread && curr->ticks_consumed > 0) {
            uint32_t x = curr->ticks_consumed;
            sum += x;
            sum_sq += (uint64_t)x * x;
            n++;
        }
        curr = curr->next;
    }

    if (n == 0 || sum_sq == 0) return 1000;

    /* J = (sum^2) / (n * sum_sq) * 1000 */
    uint64_t numerator = sum * sum * 1000;
    uint64_t denominator = (uint64_t)n * sum_sq;
    return (uint32_t)(numerator / denominator);
}

void scheduler_print_stats(void) {
    uint32_t fairness = scheduler_calculate_fairness();

    printk("\n========================================\n");
    printk("    KernelX Scheduler Telemetry\n");
    printk("========================================\n");
    printk("Active Policy       : %s\n", scheduler_get_policy_name());
    printk("Total Switches      : %u\n", total_switches);
    printk("Voluntary Yields    : %u\n", voluntary_switches);
    printk("Timer Preemptions   : %u\n", preemptive_switches);
    printk("Active Thread Count : %u\n", thread_get_count());
    uint32_t f_int = fairness / 1000;
    uint32_t f_frac = fairness % 1000;
    if (f_frac < 10) {
        printk("Jain Fairness Index : %u.00%u (1.000 = perfectly fair)\n", f_int, f_frac);
    } else if (f_frac < 100) {
        printk("Jain Fairness Index : %u.0%u (1.000 = perfectly fair)\n", f_int, f_frac);
    } else {
        printk("Jain Fairness Index : %u.%u (1.000 = perfectly fair)\n", f_int, f_frac);
    }
    printk("========================================\n\n");
}
