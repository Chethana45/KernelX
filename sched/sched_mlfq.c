#include <sched/sched.h>
#include <sched/process.h>
#include <kernel/printk.h>

#define MLFQ_BOOST_INTERVAL 50

static const uint32_t mlfq_slice_table[MLFQ_LEVELS] = { 2, 4, 8, 16 };

static thread_t* mlfq_heads[MLFQ_LEVELS];
static thread_t* mlfq_tails[MLFQ_LEVELS];
static uint32_t mlfq_boost_timer = 0;

static void mlfq_init(void) {
    for (int i = 0; i < MLFQ_LEVELS; i++) {
        mlfq_heads[i] = NULL;
        mlfq_tails[i] = NULL;
    }
    mlfq_boost_timer = 0;
}

static void mlfq_enqueue(thread_t* thread) {
    if (!thread) return;

    uint32_t lvl = thread->mlfq_level;
    if (lvl >= MLFQ_LEVELS) lvl = MLFQ_LEVELS - 1;
    thread->mlfq_level = lvl;

    thread->sched_next = NULL;
    if (mlfq_tails[lvl] == NULL) {
        mlfq_heads[lvl] = thread;
        mlfq_tails[lvl] = thread;
    } else {
        mlfq_tails[lvl]->sched_next = thread;
        mlfq_tails[lvl] = thread;
    }

    /* Preempt if higher level queue received a ready thread */
    thread_t* curr = thread_get_current();
    if (curr && curr->state == THREAD_RUNNING && lvl < curr->mlfq_level) {
        scheduler_preempt();
    }
}

static thread_t* mlfq_dequeue(void) {
    for (int lvl = 0; lvl < MLFQ_LEVELS; lvl++) {
        while (mlfq_heads[lvl] != NULL) {
            thread_t* thread = mlfq_heads[lvl];
            mlfq_heads[lvl] = thread->sched_next;
            if (mlfq_heads[lvl] == NULL) {
                mlfq_tails[lvl] = NULL;
            }
            thread->sched_next = NULL;

            if (thread->state == THREAD_READY) {
                thread->time_slice = mlfq_slice_table[thread->mlfq_level];
                thread->time_quantum = thread->time_slice;
                return thread;
            }
        }
    }
    return NULL;
}

static void mlfq_perform_boost(void) {
    /* Migrate all threads from all levels into Level 0 */
    for (int lvl = 1; lvl < MLFQ_LEVELS; lvl++) {
        thread_t* curr = mlfq_heads[lvl];
        mlfq_heads[lvl] = NULL;
        mlfq_tails[lvl] = NULL;

        while (curr != NULL) {
            thread_t* next = curr->sched_next;
            curr->sched_next = NULL;
            curr->mlfq_level = 0;
            curr->time_slice = mlfq_slice_table[0];
            curr->time_quantum = mlfq_slice_table[0];

            if (mlfq_tails[0] == NULL) {
                mlfq_heads[0] = curr;
                mlfq_tails[0] = curr;
            } else {
                mlfq_tails[0]->sched_next = curr;
                mlfq_tails[0] = curr;
            }
            curr = next;
        }
    }

    /* Also boost the currently running thread if any */
    thread_t* curr = thread_get_current();
    if (curr && curr->state == THREAD_RUNNING) {
        curr->mlfq_level = 0;
    }
}

static void mlfq_tick(thread_t* current) {
    /* Accumulate waiting time for all queued threads */
    for (int lvl = 0; lvl < MLFQ_LEVELS; lvl++) {
        thread_t* t = mlfq_heads[lvl];
        while (t != NULL) {
            if (t->state == THREAD_READY) {
                t->wait_ticks++;
            }
            t = t->sched_next;
        }
    }

    /* Periodic priority boost to prevent starvation */
    mlfq_boost_timer++;
    if (mlfq_boost_timer >= MLFQ_BOOST_INTERVAL) {
        mlfq_boost_timer = 0;
        mlfq_perform_boost();
    }

    if (!current) return;

    if (current->time_slice > 0) {
        current->time_slice--;
    }

    /* Check if a higher-priority queue has runnable threads */
    for (uint32_t lvl = 0; lvl < current->mlfq_level; lvl++) {
        if (mlfq_heads[lvl] != NULL) {
            /* Demote on slice exhaustion or preempt for higher queue */
            scheduler_preempt();
            return;
        }
    }

    if (current->time_slice == 0) {
        /* Slice exhausted: Demote to lower priority queue */
        if (current->mlfq_level < MLFQ_LEVELS - 1) {
            current->mlfq_level++;
        }
        current->time_slice = mlfq_slice_table[current->mlfq_level];
        current->time_quantum = current->time_slice;
        scheduler_preempt();
    }
}

static void mlfq_yield(thread_t* current) {
    if (current) {
        /* Voluntary yield before exhausting quantum: retain mlfq_level */
        current->time_slice = mlfq_slice_table[current->mlfq_level];
        current->time_quantum = current->time_slice;
    }
}

static thread_t* mlfq_drain_all(void) {
    thread_t* head = NULL;
    thread_t* tail = NULL;

    for (int lvl = 0; lvl < MLFQ_LEVELS; lvl++) {
        thread_t* curr = mlfq_heads[lvl];
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
        mlfq_heads[lvl] = NULL;
        mlfq_tails[lvl] = NULL;
    }
    return head;
}

sched_ops_t mlfq_ops = {
    .name = "Multilevel Feedback Queue (MLFQ)",
    .init = mlfq_init,
    .enqueue = mlfq_enqueue,
    .dequeue = mlfq_dequeue,
    .tick = mlfq_tick,
    .yield = mlfq_yield,
    .drain_all = mlfq_drain_all
};
