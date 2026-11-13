#ifndef SCHED_SCHED_H
#define SCHED_SCHED_H

#include <sched/process.h>

typedef enum {
    SCHED_POLICY_RR = 0,
    SCHED_POLICY_PRIORITY,
    SCHED_POLICY_MLFQ
} sched_policy_t;

typedef struct sched_ops {
    const char*     name;
    void            (*init)(void);
    void            (*enqueue)(thread_t* thread);
    thread_t*       (*dequeue)(void);
    void            (*tick)(thread_t* current);
    void            (*yield)(thread_t* current);
    thread_t*       (*drain_all)(void);
} sched_ops_t;

/* Scheduler Core API */
void            scheduler_init(void);
void            scheduler_start(void);
void            scheduler_enqueue(thread_t* thread);
void            scheduler_yield(void);
void            scheduler_tick(void);
void            scheduler_preempt(void);
bool            scheduler_is_active(void);

/* Pluggable Policy Management */
void            scheduler_set_policy(sched_policy_t policy);
sched_policy_t  scheduler_get_policy(void);
const char*     scheduler_get_policy_name(void);

/* Telemetry & Metrics */
void            scheduler_get_stats(uint32_t* total_switches, uint32_t* voluntary, uint32_t* preempts);
void            scheduler_print_stats(void);
uint32_t        scheduler_calculate_fairness(void);

/* Exported Algorithm Implementations */
extern sched_ops_t rr_ops;
extern sched_ops_t priority_ops;
extern sched_ops_t mlfq_ops;

#endif /* SCHED_SCHED_H */
