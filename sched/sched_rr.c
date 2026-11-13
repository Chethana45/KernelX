#include <sched/sched.h>
#include <sched/process.h>
#include <kernel/printk.h>

static thread_t* rr_head = NULL;
static thread_t* rr_tail = NULL;

static void rr_init(void) {
    rr_head = NULL;
    rr_tail = NULL;
}

static void rr_enqueue(thread_t* thread) {
    if (!thread) return;

    thread->sched_next = NULL;
    if (rr_tail == NULL) {
        rr_head = thread;
        rr_tail = thread;
    } else {
        rr_tail->sched_next = thread;
        rr_tail = thread;
    }
}

static thread_t* rr_dequeue(void) {
    while (rr_head != NULL) {
        thread_t* thread = rr_head;
        rr_head = rr_head->sched_next;
        if (rr_head == NULL) {
            rr_tail = NULL;
        }
        thread->sched_next = NULL;

        if (thread->state == THREAD_READY) {
            return thread;
        }
    }
    return NULL;
}

static void rr_tick(thread_t* current) {
    /* Update wait time for all threads waiting in ready queue */
    thread_t* t = rr_head;
    while (t != NULL) {
        if (t->state == THREAD_READY) {
            t->wait_ticks++;
        }
        t = t->sched_next;
    }

    if (!current) return;

    if (current->time_slice > 0) {
        current->time_slice--;
    }

    if (current->time_slice == 0) {
        current->time_slice = DEFAULT_TIME_SLICE;
        scheduler_preempt();
    }
}

static void rr_yield(thread_t* current) {
    if (current) {
        current->time_slice = DEFAULT_TIME_SLICE;
    }
}

static thread_t* rr_drain_all(void) {
    thread_t* list = rr_head;
    rr_head = NULL;
    rr_tail = NULL;
    return list;
}

sched_ops_t rr_ops = {
    .name = "Round Robin",
    .init = rr_init,
    .enqueue = rr_enqueue,
    .dequeue = rr_dequeue,
    .tick = rr_tick,
    .yield = rr_yield,
    .drain_all = rr_drain_all
};
