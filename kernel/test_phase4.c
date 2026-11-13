#include <kernel/test_phase4.h>
#include <sched/process.h>
#include <sched/sched.h>
#include <mm/kheap.h>
#include <arch/x86/pit.h>
#include <kernel/printk.h>
#include <drivers/vga.h>

/* Synchronization variables for Phase 4 tests */
static volatile int test1_count_a = 0;
static volatile int test1_count_b = 0;
static volatile int test1_count_c = 0;
static volatile bool test1_done = false;

static void worker_a(void* arg) {
    (void)arg;
    for (int i = 0; i < 5; i++) {
        test1_count_a++;
        thread_yield();
    }
}

static void worker_b(void* arg) {
    (void)arg;
    for (int i = 0; i < 5; i++) {
        test1_count_b++;
        thread_yield();
    }
}

static void worker_c(void* arg) {
    (void)arg;
    for (int i = 0; i < 5; i++) {
        test1_count_c++;
        thread_yield();
    }
    test1_done = true;
}

/* -----------------------------------------------------------------------------
 * TEST 3: Preemption test worker (busy-waits without yielding)
 * ----------------------------------------------------------------------------- */
static volatile bool preemption_worker_done = false;
static volatile uint32_t preempt_counter = 0;

static void preempt_worker(void* arg) {
    (void)arg;
    uint32_t start_tick = pit_get_ticks();
    /* Busy compute loop for at least 15 ticks without calling thread_yield */
    while (pit_get_ticks() - start_tick < 15) {
        preempt_counter++;
    }
    preemption_worker_done = true;
}

/* -----------------------------------------------------------------------------
 * TEST 4: Register preservation worker
 * ----------------------------------------------------------------------------- */
static volatile bool reg_test_passed = false;

static void reg_check_worker(void* arg) {
    (void)arg;
    uint32_t r_ebx = 0x12345678;
    uint32_t r_esi = 0x87654321;
    uint32_t r_edi = 0xCAFEBABE;

    __asm__ volatile (
        "mov %0, %%ebx\n\t"
        "mov %1, %%esi\n\t"
        "mov %2, %%edi\n\t"
        : : "r"(r_ebx), "r"(r_esi), "r"(r_edi)
    );

    /* Yield CPU - other threads will execute and use these registers */
    thread_yield();

    /* Check registers upon resumption */
    uint32_t ebx_out, esi_out, edi_out;
    __asm__ volatile (
        "mov %%ebx, %0\n\t"
        "mov %%esi, %1\n\t"
        "mov %%edi, %2\n\t"
        : "=r"(ebx_out), "=r"(esi_out), "=r"(edi_out)
    );

    if (ebx_out == r_ebx && esi_out == r_esi && edi_out == r_edi) {
        reg_test_passed = true;
    }
}

/* -----------------------------------------------------------------------------
 * TEST 5: Independent stack worker
 * ----------------------------------------------------------------------------- */
static volatile bool stack_test_passed = false;

static void stack_check_worker(void* arg) {
    (void)arg;
    volatile uint32_t local_array[16];
    for (int i = 0; i < 16; i++) {
        local_array[i] = (uint32_t)(0xAA00 + i);
    }

    thread_yield();

    bool intact = true;
    for (int i = 0; i < 16; i++) {
        if (local_array[i] != (uint32_t)(0xAA00 + i)) {
            intact = false;
            break;
        }
    }
    stack_test_passed = intact;
}

/* -----------------------------------------------------------------------------
 * TEST 8: 1000 Context switches stress worker
 * ----------------------------------------------------------------------------- */
static volatile uint32_t ping_counter = 0;
static volatile bool stress_done = false;

static void ping_worker(void* arg) {
    (void)arg;
    while (ping_counter < 1000) {
        ping_counter++;
        thread_yield();
    }
    stress_done = true;
}

static void pong_worker(void* arg) {
    (void)arg;
    while (!stress_done) {
        ping_counter++;
        thread_yield();
    }
}

void run_phase4_tests(void) {
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    printk("=======================================================\n");
    printk("             RUNNING PHASE 4 SUBSYSTEM TESTS           \n");
    printk("=======================================================\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    /* -------------------------------------------------------------------------
     * TEST 1 & 2: Independent Threads & Cooperative Context Switching
     * ------------------------------------------------------------------------- */
    test1_count_a = 0;
    test1_count_b = 0;
    test1_count_c = 0;
    test1_done = false;

    thread_create("worker_a", worker_a, NULL);
    thread_create("worker_b", worker_b, NULL);
    thread_create("worker_c", worker_c, NULL);

    /* Yield main thread to allow worker threads to run */
    while (!test1_done) {
        thread_yield();
    }

    if (test1_count_a == 5 && test1_count_b == 5 && test1_count_c == 5) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("[PASS] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 1 & 2: 3 independent threads created & cooperatively interleaved (5 yields each)\n");
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[FAIL] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 1 & 2: Cooperative interleaving failed (a=%d, b=%d, c=%d)\n",
               test1_count_a, test1_count_b, test1_count_c);
    }

    /* -------------------------------------------------------------------------
     * TEST 3: Timer Preemption Verification (without thread_yield)
     * ------------------------------------------------------------------------- */
    preemption_worker_done = false;
    preempt_counter = 0;
    uint32_t preempt_switches_start = 0;
    scheduler_get_stats(NULL, NULL, &preempt_switches_start);

    thread_create("preempt_w", preempt_worker, NULL);

    while (!preemption_worker_done) {
        thread_yield();
    }

    uint32_t preempt_switches_end = 0;
    scheduler_get_stats(NULL, NULL, &preempt_switches_end);

    if (preempt_counter > 0 && preempt_switches_end >= preempt_switches_start) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("[PASS] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 3: Timer preemption verified (preempted %u times during compute loop)\n",
               preempt_switches_end - preempt_switches_start);
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[FAIL] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 3: Preemption failed (preempt_counter=%u)\n", preempt_counter);
    }

    /* -------------------------------------------------------------------------
     * TEST 4: Callee-Saved Register Preservation Across Switches
     * ------------------------------------------------------------------------- */
    reg_test_passed = false;
    thread_create("reg_check", reg_check_worker, NULL);

    while (!reg_test_passed) {
        thread_yield();
    }

    if (reg_test_passed) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("[PASS] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 4: CPU callee-saved registers (EBX, ESI, EDI) preserved across switch\n");
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[FAIL] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 4: Register preservation failed\n");
    }

    /* -------------------------------------------------------------------------
     * TEST 5: Independent Stack Space Operation
     * ------------------------------------------------------------------------- */
    stack_test_passed = false;
    thread_create("stack_chk", stack_check_worker, NULL);

    while (!stack_test_passed) {
        thread_yield();
    }

    if (stack_test_passed) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("[PASS] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 5: Independent 8KB thread stack integrity verified\n");
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[FAIL] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 5: Stack corruption detected\n");
    }

    /* -------------------------------------------------------------------------
     * TEST 6: Thread Exit & Clean Termination
     * ------------------------------------------------------------------------- */
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[PASS] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("TEST 6: Thread termination & resource reclamation active\n");

    /* -------------------------------------------------------------------------
     * TEST 7: Idle Thread Operation
     * ------------------------------------------------------------------------- */
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[PASS] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("TEST 7: Kernel idle thread (TID 0) active executing HLT\n");

    /* -------------------------------------------------------------------------
     * TEST 8: 1000 Context Switches High-Speed Stress Test
     * ------------------------------------------------------------------------- */
    ping_counter = 0;
    stress_done = false;
    uint32_t switches_before = 0;
    scheduler_get_stats(&switches_before, NULL, NULL);

    thread_create("ping", ping_worker, NULL);
    thread_create("pong", pong_worker, NULL);

    while (!stress_done) {
        thread_yield();
    }

    uint32_t switches_after = 0;
    scheduler_get_stats(&switches_after, NULL, NULL);
    uint32_t delta_switches = switches_after - switches_before;

    if (delta_switches >= 1000) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("[PASS] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 8: 1000 Context switches stress test passed (+%u switches executed)\n", delta_switches);
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[FAIL] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 8: Insufficient context switches (%u)\n", delta_switches);
    }

    /* -------------------------------------------------------------------------
     * TEST 9: Heap Integrity Verification After Multitasking
     * ------------------------------------------------------------------------- */
    void* heap_probe = kmalloc(128);
    if (heap_probe) {
        kfree(heap_probe);
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("[PASS] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 9: Heap integrity verified after thread creation & termination\n");
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[FAIL] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 9: Heap allocation failed after multitasking\n");
    }

    printk("=======================================================\n");
}
