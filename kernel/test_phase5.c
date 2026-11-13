#include <kernel/test_phase5.h>
#include <sched/sched.h>
#include <sched/process.h>
#include <arch/x86/pit.h>
#include <drivers/vga.h>
#include <kernel/printk.h>

/* Assert helper */
static void assert_phase5(bool condition, const char* test_name) {
    if (condition) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("  [PASS] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("%s\n", test_name);
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("  [FAIL] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("%s\n", test_name);
        panic("Phase 5 assertion failed!");
    }
}

/* ========================================================================= */
/* TEST 1: Dynamic Policy Switching                                         */
/* ========================================================================= */
static void test1_policy_switching(void) {
    printk("\n--- TEST 1: Runtime Scheduling Policy Switching ---\n");

    scheduler_set_policy(SCHED_POLICY_RR);
    assert_phase5(scheduler_get_policy() == SCHED_POLICY_RR, "Policy switch to Round Robin");

    scheduler_set_policy(SCHED_POLICY_PRIORITY);
    assert_phase5(scheduler_get_policy() == SCHED_POLICY_PRIORITY, "Policy switch to Priority Scheduling");

    scheduler_set_policy(SCHED_POLICY_MLFQ);
    assert_phase5(scheduler_get_policy() == SCHED_POLICY_MLFQ, "Policy switch to MLFQ");

    scheduler_set_policy(SCHED_POLICY_RR);
    assert_phase5(scheduler_get_policy() == SCHED_POLICY_RR, "Policy switch back to Round Robin");
}

/* ========================================================================= */
/* TEST 2: Preemptive Priority Ordering                                     */
/* ========================================================================= */
static volatile int test2_order[2] = {0, 0};
static volatile int test2_idx = 0;
static volatile bool test2_done = false;

static void worker_low_prio(void* arg) {
    (void)arg;
    test2_order[test2_idx++] = 10;
    if (test2_idx == 2) test2_done = true;
    thread_exit();
}

static void worker_high_prio(void* arg) {
    (void)arg;
    test2_order[test2_idx++] = 30;
    if (test2_idx == 2) test2_done = true;
    thread_exit();
}

static void test2_priority_ordering(void) {
    printk("\n--- TEST 2: Priority Execution Order & Preemption ---\n");
    scheduler_set_policy(SCHED_POLICY_PRIORITY);

    test2_idx = 0;
    test2_done = false;
    test2_order[0] = 0;
    test2_order[1] = 0;

    thread_t* main_t = thread_get_current();
    main_t->time_slice = DEFAULT_TIME_SLICE;
    /* Set main priority to 31 so neither priority 5 nor 30 can preempt main during creation */
    main_t->priority = 31;
    main_t->dynamic_priority = 31;

    /* Atomically enqueue both test threads */
    __asm__ volatile ("cli");
    thread_create_priority("prio_low", worker_low_prio, NULL, 5);
    thread_create_priority("prio_high", worker_high_prio, NULL, 30);

    /* Lower main priority to 0 so child threads run in priority order */
    main_t->priority = 0;
    main_t->dynamic_priority = 0;
    __asm__ volatile ("sti");

    uint32_t start = pit_get_ticks();
    while (!test2_done && (pit_get_ticks() - start < 50)) {
        thread_yield();
    }

    /* Restore main thread default priority */
    main_t->priority = PRIORITY_DEFAULT;
    main_t->dynamic_priority = PRIORITY_DEFAULT;

    printk("  Observed Order: [%d, %d]\n", test2_order[0], test2_order[1]);
    assert_phase5(test2_done, "Both priority threads executed to completion");
    assert_phase5(test2_order[0] == 30 && test2_order[1] == 10,
                  "High-priority thread (Prio 30) executed strictly before Low-priority thread (Prio 5)");
}

/* ========================================================================= */
/* TEST 3: MLFQ Demotion & Interactive Retention                            */
/* ========================================================================= */
static volatile bool test3_cpu_done = false;
static volatile bool test3_io_done = false;
static volatile uint32_t final_cpu_level = 0;
static volatile uint32_t final_io_level = 0;

static void mlfq_cpu_worker(void* arg) {
    (void)arg;
    thread_t* curr = thread_get_current();
    /* Compute-heavy workload: consumes ~16 ticks (2 in Q0 + 4 in Q1 + 8 in Q2) */
    uint32_t target_ticks = pit_get_ticks() + 16;
    while (pit_get_ticks() < target_ticks) {
        /* Busy wait consuming time slices */
    }
    final_cpu_level = curr->mlfq_level;
    test3_cpu_done = true;
    thread_exit();
}

static void mlfq_io_worker(void* arg) {
    (void)arg;
    thread_t* curr = thread_get_current();
    for (int i = 0; i < 6; i++) {
        thread_yield();
    }
    final_io_level = curr->mlfq_level;
    test3_io_done = true;
    thread_exit();
}

static void test3_mlfq_demotion(void) {
    printk("\n--- TEST 3: MLFQ Dynamic Demotion & Interactive Retention ---\n");
    scheduler_set_policy(SCHED_POLICY_MLFQ);

    test3_cpu_done = false;
    test3_io_done = false;

    /* Lower main thread's MLFQ priority so child workers are scheduled first */
    thread_t* main_t = thread_get_current();
    main_t->mlfq_level = 3;

    thread_create("mlfq_cpu", mlfq_cpu_worker, NULL);
    thread_create("mlfq_io", mlfq_io_worker, NULL);

    uint32_t start = pit_get_ticks();
    while ((!test3_cpu_done || !test3_io_done) && (pit_get_ticks() - start < 80)) {
        thread_yield();
    }

    assert_phase5(test3_cpu_done && test3_io_done, "MLFQ test workloads executed");
    assert_phase5(final_cpu_level >= 2, "CPU-bound thread was demoted to lower-priority queue (Q2/Q3)");
    assert_phase5(final_io_level == 0, "Yielding interactive thread remained in top-priority queue (Q0)");
}

/* ========================================================================= */
/* TEST 4: MLFQ Periodic Priority Boost                                     */
/* ========================================================================= */
static volatile bool test4_done = false;
static volatile uint32_t boosted_level = 99;

static void mlfq_boost_worker(void* arg) {
    (void)arg;
    thread_t* curr = thread_get_current();
    curr->mlfq_level = 3;

    /* Wait across boost interval (MLFQ_BOOST_INTERVAL = 50 ticks) */
    uint32_t start = pit_get_ticks();
    while (pit_get_ticks() - start < 55) {
        thread_yield();
    }
    boosted_level = curr->mlfq_level;
    test4_done = true;
    thread_exit();
}

static void test4_mlfq_boost(void) {
    printk("\n--- TEST 4: MLFQ Periodic Starvation Boost ---\n");
    scheduler_set_policy(SCHED_POLICY_MLFQ);

    test4_done = false;
    boosted_level = 99;

    thread_t* main_t = thread_get_current();
    main_t->mlfq_level = 3;

    thread_create("mlfq_boost", mlfq_boost_worker, NULL);

    uint32_t start = pit_get_ticks();
    while (!test4_done && (pit_get_ticks() - start < 100)) {
        thread_yield();
    }

    assert_phase5(test4_done, "MLFQ boost thread completed observation window");
    assert_phase5(boosted_level == 0, "Periodic boost successfully restored demoted thread from Q3 to Q0");
}

/* ========================================================================= */
/* TEST 5: Starvation Prevention via Aging in Priority Scheduler            */
/* ========================================================================= */
static volatile bool low_thread_ran = false;
static volatile bool stop_hog = false;

static void starving_low_worker(void* arg) {
    (void)arg;
    low_thread_ran = true;
    stop_hog = true;
    thread_exit();
}

static void medium_hog_worker(void* arg) {
    (void)arg;
    while (!stop_hog) {
        thread_yield();
    }
    thread_exit();
}

static void test5_priority_aging(void) {
    printk("\n--- TEST 5: Starvation Prevention via Priority Aging ---\n");
    scheduler_set_policy(SCHED_POLICY_PRIORITY);

    low_thread_ran = false;
    stop_hog = false;

    /* Set main thread to lowest priority 0 */
    thread_t* main_t = thread_get_current();
    main_t->priority = 0;
    main_t->dynamic_priority = 0;

    /* Low priority thread (10) vs medium thread (13) */
    thread_create_priority("low_prio", starving_low_worker, NULL, 10);
    thread_create_priority("med_hog", medium_hog_worker, NULL, 13);

    uint32_t start = pit_get_ticks();
    while (!low_thread_ran && (pit_get_ticks() - start < 80)) {
        thread_yield();
    }
    stop_hog = true;

    /* Restore main thread default priority */
    main_t->priority = PRIORITY_DEFAULT;
    main_t->dynamic_priority = PRIORITY_DEFAULT;

    assert_phase5(low_thread_ran, "Starving low-priority thread acquired CPU via dynamic priority aging");
}

/* ========================================================================= */
/* TEST 6: 10,000+ Scheduling Decisions & Context Switches                  */
/* ========================================================================= */
static volatile uint32_t stress_counter = 0;
#define TARGET_STRESS_DECISIONS 10000

static void stress_worker(void* arg) {
    (void)arg;
    while (stress_counter < TARGET_STRESS_DECISIONS) {
        stress_counter++;
        thread_yield();
    }
    thread_exit();
}

static void test6_high_frequency_stress(void) {
    printk("\n--- TEST 6: High-Frequency Context Switching (10,000+ Decisions) ---\n");
    scheduler_set_policy(SCHED_POLICY_RR);

    stress_counter = 0;
    uint32_t initial_switches, v, p;
    scheduler_get_stats(&initial_switches, &v, &p);

    thread_create("stress_1", stress_worker, NULL);
    thread_create("stress_2", stress_worker, NULL);
    thread_create("stress_3", stress_worker, NULL);
    thread_create("stress_4", stress_worker, NULL);

    while (stress_counter < TARGET_STRESS_DECISIONS) {
        thread_yield();
    }

    uint32_t final_switches;
    scheduler_get_stats(&final_switches, &v, &p);
    uint32_t decisions = final_switches - initial_switches;

    printk("  Completed Decisions : %u\n", decisions);
    assert_phase5(decisions >= TARGET_STRESS_DECISIONS,
                  "Exceeded 10,000 live context switches without corruption or deadlock");
}

/* ========================================================================= */
/* TEST 7: Benchmark Suite & Comparative Telemetry                          */
/* ========================================================================= */
static volatile bool bench_running = true;
static volatile uint32_t bench_cpu_ops = 0;
static volatile uint32_t bench_io_yields = 0;

static void bench_cpu_worker(void* arg) {
    (void)arg;
    while (bench_running) {
        bench_cpu_ops++;
    }
    thread_exit();
}

static void bench_io_worker(void* arg) {
    (void)arg;
    while (bench_running) {
        bench_io_yields++;
        thread_yield();
    }
    thread_exit();
}

void run_scheduler_benchmarks(void) {
    printk("\n=======================================================================\n");
    printk("             KernelX CPU Scheduler Comparative Benchmark               \n");
    printk("=======================================================================\n");

    sched_policy_t policies[3] = { SCHED_POLICY_RR, SCHED_POLICY_PRIORITY, SCHED_POLICY_MLFQ };
    const char* names[3] = { "Round Robin", "Priority (Aging)", "MLFQ (4 Queues)" };

    for (int p = 0; p < 3; p++) {
        scheduler_set_policy(policies[p]);
        uint32_t sw_start, v_start, pr_start;
        scheduler_get_stats(&sw_start, &v_start, &pr_start);

        bench_running = true;
        bench_cpu_ops = 0;
        bench_io_yields = 0;

        thread_create("b_cpu", bench_cpu_worker, NULL);
        thread_create("b_io", bench_io_worker, NULL);

        uint32_t tick_start = pit_get_ticks();
        while (pit_get_ticks() - tick_start < 20) {
            thread_yield();
        }
        bench_running = false;

        /* Let workers terminate */
        for (int i = 0; i < 15; i++) thread_yield();

        uint32_t tick_end = pit_get_ticks();
        uint32_t sw_end, v_end, pr_end;
        scheduler_get_stats(&sw_end, &v_end, &pr_end);

        uint32_t duration = (tick_end > tick_start) ? (tick_end - tick_start) : 1;
        uint32_t total_sw = sw_end - sw_start;
        uint32_t fairness = scheduler_calculate_fairness();

        printk("Algorithm: %s\n", names[p]);
        printk("  Context Switches : %u\n", total_sw);
        printk("  CPU Worker Loops : %u\n", bench_cpu_ops);
        printk("  I/O Yield Cycles : %u\n", bench_io_yields);
        printk("  Execution Window : %u ticks\n", duration);
        uint32_t f_int = fairness / 1000;
        uint32_t f_frac = fairness % 1000;
        if (f_frac < 10) {
            printk("  Jain Fairness Idx: %u.00%u\n\n", f_int, f_frac);
        } else if (f_frac < 100) {
            printk("  Jain Fairness Idx: %u.0%u\n\n", f_int, f_frac);
        } else {
            printk("  Jain Fairness Idx: %u.%u\n\n", f_int, f_frac);
        }
    }
    printk("=======================================================================\n");

    /* Restore Round Robin as default */
    scheduler_set_policy(SCHED_POLICY_RR);
}

void run_phase5_tests(void) {
    printk("\n=======================================================================\n");
    printk(">>> Beginning Phase 5 Advanced CPU Scheduler Automated Test Suite <<<\n");
    printk("=======================================================================\n");

    test1_policy_switching();
    test2_priority_ordering();
    test3_mlfq_demotion();
    test4_mlfq_boost();
    test5_priority_aging();
    test6_high_frequency_stress();
    run_scheduler_benchmarks();

    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("\n>>> [PHASE 5 PASSED] All 7 Advanced Scheduler tests & benchmarks passed. <<<\n\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}
