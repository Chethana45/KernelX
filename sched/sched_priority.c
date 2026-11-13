#include <sched/sched.h>
#include <sched/process.h>
#include <kernel/printk.h>

#define AGING_INTERVAL_TICKS 5

static thread_t* prio_heads[32];
static thread_t* prio_tails[32];
static uint32_t prio_active_mask = 0;
static uint32_t aging_tick_counter = 0;

static void priority_init(void) {
    for (int i = 0; i < 32; i++) {
        prio_heads[i] = NULL;
        prio_tails[i] = NULL;
    }
    prio_active_mask = 0;
    aging_tick_counter = 0;
}

static void prio_enqueue_level(thread_t* thread, uint32_t level) {
    if (level > 31) level = 31;
    thread->sched_next = NULL;

    if (prio_tails[level] == NULL) {
        prio_heads[level] = thread;
        prio_tails[level] = thread;
    } else {
        prio_tails[level]->sched_next = thread;
        prio_tails[level] = thread;
    }
    prio_active_mask |= (1U << level);
}

static void priority_enqueue(thread_t* thread) {
    if (!thread) return;

    uint32_t level = thread->dynamic_priority;
    if (level > 31) level = 31;

    prio_enqueue_level(thread, level);

    /* If newly ready thread has higher priority than currently running thread, preempt */
    thread_t* curr = thread_get_current();
    if (curr && curr->state == THREAD_RUNNING && level > curr->dynamic_priority) {
        scheduler_preempt();
    }
}

static thread_t* priority_dequeue(void) {
    while (prio_active_mask != 0) {
        /* Find highest priority non-empty queue (bit 31 is highest) */
        int highest = 31 - __builtin_clz(prio_active_mask);

        thread_t* thread = prio_heads[highest];
        if (thread != NULL) {
            prio_heads[highest] = thread->sched_next;
            if (prio_heads[highest] == NULL) {
                prio_tails[highest] = NULL;
                prio_active_mask &= ~(1U << highest);
            }
            thread->sched_next = NULL;

            if (thread->state == THREAD_READY) {
                /* Reset dynamic priority to base priority upon scheduling */
                thread->dynamic_priority = thread->priority;
                return thread;
            }
        } else {
            prio_active_mask &= ~(1U << highest);
        }
    }
    return NULL;
}

static void priority_perform_aging(void) {
    /* Migrate waiting threads from queues < 31 up by 1 level */
    for (int lvl = 30; lvl >= 0; lvl--) {
        if ((prio_active_mask & (1U << lvl)) == 0) continue;

        thread_t* curr = prio_heads[lvl];
        prio_heads[lvl] = NULL;
        prio_tails[lvl] = NULL;
        prio_active_mask &= ~(1U << lvl);

        while (curr != NULL) {
            thread_t* next = curr->sched_next;
            curr->sched_next = NULL;

            if (curr->state == THREAD_READY) {
                curr->dynamic_priority = lvl + 1;
                prio_enqueue_level(curr, curr->dynamic_priority);
            }
            curr = next;
        }
    }
}

static void priority_tick(thread_t* current) {
    /* Accumulate waiting time across all priority levels */
    for (int i = 0; i < 32; i++) {
        thread_t* t = prio_heads[i];
        while (t != NULL) {
            if (t->state == THREAD_READY) {
                t->wait_ticks++;
            }
            t = t->sched_next;
        }
    }

    /* Aging periodic trigger */
    aging_tick_counter++;
    if (aging_tick_counter >= AGING_INTERVAL_TICKS) {
        aging_tick_counter = 0;
        priority_perform_aging();
    }

    if (!current) return;

    if (current->time_slice > 0) {
        current->time_slice--;
    }

    /* Check if a higher priority thread is waiting */
    if (prio_active_mask != 0) {
        int highest = 31 - __builtin_clz(prio_active_mask);
        if ((uint32_t)highest > current->dynamic_priority) {
            current->time_slice = DEFAULT_TIME_SLICE;
            scheduler_preempt();
            return;
        }
    }

    if (current->time_slice == 0) {
        current->time_slice = DEFAULT_TIME_SLICE;
        scheduler_preempt();
    }
}

static void priority_yield(thread_t* current) {
    if (current) {
        current->time_slice = DEFAULT_TIME_SLICE;
    }
}

static thread_t* priority_drain_all(void) {
    thread_t* head = NULL;
    thread_t* tail = NULL;

    for (int i = 0; i < 32; i++) {
        thread_t* curr = prio_heads[i];
        while (curr != NULL) {
            thread_t* next = curr->sched_next;
            curr->sched_next = NULL;

            if (tail == NULL) {
                head = curr;
                tail = curr;
            } else {
                tail->sched_next = curr;
                tail = curr;
            }
            curr = next;
        }
        prio_heads[i] = NULL;
        prio_tails[i] = NULL;
    }
    prio_active_mask = 0;
    return head;
}

sched_ops_t priority_ops = {
    .name = "Preemptive Priority (Aging)",
    .init = priority_init,
    .enqueue = priority_enqueue,
    .dequeue = priority_dequeue,
    .tick = priority_tick,
    .yield = priority_yield,
    .drain_all = priority_drain_all
};
