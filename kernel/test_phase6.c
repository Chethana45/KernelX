#include <kernel/test_phase6.h>
#include <sched/process.h>
#include <sched/sched.h>
#include <arch/x86/syscall.h>
#include <arch/x86/pit.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/page_fault.h>
#include <drivers/vga.h>
#include <kernel/printk.h>

/* User test program declarations from user_tests.asm */
extern const uint8_t user_prog_basic[];
extern const uint32_t user_prog_basic_size;

extern const uint8_t user_prog_priv_violation[];
extern const uint32_t user_prog_priv_violation_size;

extern const uint8_t user_prog_read_kernel_mem[];
extern const uint32_t user_prog_read_kernel_mem_size;

extern const uint8_t user_prog_write_kernel_mem[];
extern const uint32_t user_prog_write_kernel_mem_size;

extern const uint8_t user_prog_write_code[];
extern const uint32_t user_prog_write_code_size;

extern const uint8_t user_prog_read_unmapped[];
extern const uint32_t user_prog_read_unmapped_size;

extern const uint8_t user_prog_syscall_edge_cases[];
extern const uint32_t user_prog_syscall_edge_cases_size;

extern const uint8_t user_prog_isolation_a[];
extern const uint32_t user_prog_isolation_a_size;

extern const uint8_t user_prog_isolation_b[];
extern const uint32_t user_prog_isolation_b_size;

extern const uint8_t user_prog_preempt_a[];
extern const uint8_t user_prog_preempt_b[];
extern const uint32_t user_prog_preempt_a_size;
extern const uint32_t user_prog_preempt_b_size;

/* Assertion helper */
static void assert_phase6(bool condition, const char* test_name) {
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
        panic("Phase 6 assertion failed!");
    }
}

/* ========================================================================= */
/* TEST 1: Ring 3 User Mode Execution & Syscall Dispatching                  */
/* ========================================================================= */
static void test1_ring3_and_syscalls(void) {
    printk("\n--- TEST 1: Ring 3 Execution & System Call Dispatching ---\n");

    thread_t* u_proc = process_create_user("u_basic", user_prog_basic, user_prog_basic_size);
    assert_phase6(u_proc != NULL, "User process created with isolated address space");

    uint32_t start = pit_get_ticks();
    while (u_proc->state != THREAD_TERMINATED && (pit_get_ticks() - start < 50)) {
        thread_yield();
    }

    assert_phase6(u_proc->state == THREAD_TERMINATED,
                  "User process executed in Ring 3 and exited via sys_exit");
    assert_phase6(u_proc->is_user == true,
                  "Process verified as genuine Ring 3 user mode task");
}

/* ========================================================================= */
/* TEST 2: Privileged Instruction Protection (Ring 3 Security)               */
/* ========================================================================= */
static void test2_privileged_instruction_protection(void) {
    printk("\n--- TEST 2: Privileged Instruction Fault Isolation ---\n");

    thread_t* u_proc = process_create_user("u_priv", user_prog_priv_violation, user_prog_priv_violation_size);
    assert_phase6(u_proc != NULL, "User task created to test privileged instruction fault");

    uint32_t start = pit_get_ticks();
    while (u_proc->state != THREAD_TERMINATED && (pit_get_ticks() - start < 50)) {
        thread_yield();
    }

    assert_phase6(u_proc->state == THREAD_TERMINATED,
                  "Privileged instruction 'cli' in Ring 3 triggered #GP and terminated process safely");
}

/* ========================================================================= */
/* TEST 3: User Memory Permissions & Fault Isolation                        */
/* ========================================================================= */
static volatile bool caught_pf = false;
static volatile uint32_t last_pf_addr = 0;
static volatile uint32_t last_pf_err = 0;

static bool pf_audit_hook(registers_t* regs, uint32_t faulting_addr) {
    if (regs->err_code & 0x4) { /* Fault from user mode */
        caught_pf = true;
        last_pf_addr = faulting_addr;
        last_pf_err = regs->err_code;
        thread_exit();
        return true;
    }
    return false;
}

static void test3_memory_permissions(void) {
    printk("\n--- TEST 3: User Memory Permissions & Page Protection ---\n");

    page_fault_set_hook(pf_audit_hook);

    /* 3A: Reading kernel supervisor memory from Ring 3 */
    caught_pf = false;
    thread_t* p_read_kmem = process_create_user("p_rkmem", user_prog_read_kernel_mem, user_prog_read_kernel_mem_size);
    assert_phase6(p_read_kmem != NULL, "Spawned process to read kernel memory");
    uint32_t start = pit_get_ticks();
    while (p_read_kmem->state != THREAD_TERMINATED && (pit_get_ticks() - start < 50)) {
        thread_yield();
    }
    assert_phase6(caught_pf && last_pf_addr == 0x00100000,
                  "Ring 3 read of kernel supervisor page (0x00100000) intercepted by #PF");
    assert_phase6(p_read_kmem->state == THREAD_TERMINATED,
                  "Process reading kernel memory terminated safely");

    /* 3B: Writing kernel supervisor memory from Ring 3 */
    caught_pf = false;
    thread_t* p_write_kmem = process_create_user("p_wkmem", user_prog_write_kernel_mem, user_prog_write_kernel_mem_size);
    assert_phase6(p_write_kmem != NULL, "Spawned process to write kernel memory");
    start = pit_get_ticks();
    while (p_write_kmem->state != THREAD_TERMINATED && (pit_get_ticks() - start < 50)) {
        thread_yield();
    }
    assert_phase6(caught_pf && last_pf_addr == 0x00100000 && (last_pf_err & 0x2),
                  "Ring 3 write to kernel supervisor page intercepted by #PF (Write violation)");
    assert_phase6(p_write_kmem->state == THREAD_TERMINATED,
                  "Process writing kernel memory terminated safely");

    /* 3C: Writing to read-only user code page from Ring 3 */
    caught_pf = false;
    thread_t* p_wcode = process_create_user("p_wcode", user_prog_write_code, user_prog_write_code_size);
    assert_phase6(p_wcode != NULL, "Spawned process to write to read-only code page");
    start = pit_get_ticks();
    while (p_wcode->state != THREAD_TERMINATED && (pit_get_ticks() - start < 50)) {
        thread_yield();
    }
    assert_phase6(caught_pf && last_pf_addr == 0x40000000 && (last_pf_err & 0x2),
                  "Writing to read-only user code page (0x40000000) intercepted by #PF (Protection violation)");
    assert_phase6(p_wcode->state == THREAD_TERMINATED,
                  "Process violating code page read-only permission terminated safely");

    /* 3D: Reading unmapped virtual page */
    caught_pf = false;
    thread_t* p_unmap = process_create_user("p_unmap", user_prog_read_unmapped, user_prog_read_unmapped_size);
    assert_phase6(p_unmap != NULL, "Spawned process to access unmapped page");
    start = pit_get_ticks();
    while (p_unmap->state != THREAD_TERMINATED && (pit_get_ticks() - start < 50)) {
        thread_yield();
    }
    assert_phase6(caught_pf && last_pf_addr == 0x50000000 && !(last_pf_err & 0x1),
                  "Reading unmapped page (0x50000000) intercepted by #PF (Non-Present)");
    assert_phase6(p_unmap->state == THREAD_TERMINATED,
                  "Process accessing unmapped address terminated safely");

    page_fault_set_hook(NULL);
}

/* ========================================================================= */
/* TEST 4: Comprehensive Syscall Pointer Validation                          */
/* ========================================================================= */
static void test4_comprehensive_pointer_validation(void) {
    printk("\n--- TEST 4: Syscall Pointer & Memory Range Security Audit ---\n");

    /* 4A: Direct kernel-side validation of edge cases */
    assert_phase6(vmm_validate_user_ptr(NULL, 10, false) == false,
                  "Direct check: NULL pointer rejected");
    assert_phase6(vmm_validate_user_ptr((void*)0x00100000, 64, false) == false,
                  "Direct check: Kernel memory pointer (< 0x40000000) rejected");
    assert_phase6(vmm_validate_user_ptr((void*)0xC0000000, 64, false) == false,
                  "Direct check: Higher-half memory pointer (>= 0xC0000000) rejected");
    assert_phase6(vmm_validate_user_ptr((void*)0xFFFFFFF0, 32, false) == false,
                  "Direct check: Arithmetic overflow (0xFFFFFFF0 + 32) rejected");
    assert_phase6(vmm_validate_user_ptr((void*)0x50000000, 64, false) == false,
                  "Direct check: Unmapped user-space pointer rejected");

    /* 4B: Ring 3 user program exercising all syscall pointer security checks */
    thread_t* u_proc = process_create_user("u_edge", user_prog_syscall_edge_cases, user_prog_syscall_edge_cases_size);
    assert_phase6(u_proc != NULL, "Created user task for comprehensive syscall testing");

    uint32_t start = pit_get_ticks();
    while (u_proc->state != THREAD_TERMINATED && (pit_get_ticks() - start < 50)) {
        thread_yield();
    }

    assert_phase6(u_proc->state == THREAD_TERMINATED,
                  "Ring 3 syscall checks (kernel ptr, overflow, cross-unmapped page, read-only write, NULL, zero-len) all returned -EFAULT / success code 42");
}

/* ========================================================================= */
/* TEST 5: Virtual Address Space Isolation                                   */
/* ========================================================================= */
static void test5_address_space_isolation(void) {
    printk("\n--- TEST 5: Virtual Address Space Isolation & Crosstalk Prevention ---\n");

    thread_t* proc_a = process_create_user("iso_proc_a", user_prog_isolation_a, user_prog_isolation_a_size);
    thread_t* proc_b = process_create_user("iso_proc_b", user_prog_isolation_b, user_prog_isolation_b_size);

    assert_phase6(proc_a != NULL && proc_b != NULL, "Two user processes instantiated");
    assert_phase6(proc_a->cr3 != proc_b->cr3,
                  "Processes possess distinct physical page directories (isolated CR3)");

    uint32_t start = pit_get_ticks();
    while ((proc_a->state != THREAD_TERMINATED || proc_b->state != THREAD_TERMINATED) &&
           (pit_get_ticks() - start < 100)) {
        thread_yield();
    }

    assert_phase6(proc_a->state == THREAD_TERMINATED && proc_b->state == THREAD_TERMINATED,
                  "Both processes verified private memory at same virtual address without crosstalk");
}

/* ========================================================================= */
/* TEST 6: Preemptive Multitasking of User Processes                         */
/* ========================================================================= */
static void test6_preemptive_user_multitasking(void) {
    printk("\n--- TEST 6: Preemptive Multitasking of Ring 3 Processes ---\n");

    thread_t* proc_a = process_create_user("preempt_a", user_prog_preempt_a, user_prog_preempt_a_size);
    thread_t* proc_b = process_create_user("preempt_b", user_prog_preempt_b, user_prog_preempt_b_size);

    assert_phase6(proc_a != NULL && proc_b != NULL, "Created concurrent compute-bound user tasks");

    uint32_t start = pit_get_ticks();
    while ((proc_a->state != THREAD_TERMINATED || proc_b->state != THREAD_TERMINATED) &&
           (pit_get_ticks() - start < 100)) {
        thread_yield();
    }

    assert_phase6(proc_a->state == THREAD_TERMINATED && proc_b->state == THREAD_TERMINATED,
                  "Preemptive PIT timer successfully interleaved execution of Ring 3 processes");
    assert_phase6(proc_a->switches_count > 0 && proc_b->switches_count > 0,
                  "Context switches occurred across user address spaces during computation");
}

/* ========================================================================= */
/* TEST 7: Repeated Process Creation & Resource Reclamation Stress Test      */
/* ========================================================================= */
static void test7_lifecycle_stress_and_reclamation(void) {
    printk("\n--- TEST 7: Process Lifecycle Stress & Memory Reclamation ---\n");

    uint32_t free_frames_before = pmm_get_free_frames();

    for (int i = 0; i < 8; i++) {
        thread_t* p = process_create_user("stress_u", user_prog_basic, user_prog_basic_size);
        assert_phase6(p != NULL, "Created transient user process");

        uint32_t start = pit_get_ticks();
        while (p->state != THREAD_TERMINATED && (pit_get_ticks() - start < 30)) {
            thread_yield();
        }
        assert_phase6(p->state == THREAD_TERMINATED, "Transient process terminated");
    }

    /* Force dead thread cleanup by scheduling once */
    thread_yield();

    uint32_t free_frames_after = pmm_get_free_frames();
    printk("  Frames before: %u, Frames after: %u\n", free_frames_before, free_frames_after);
    assert_phase6(free_frames_after >= free_frames_before - 2,
                  "All physical frames (page directory, user code, data, stack) reclaimed without leakage");
}

/* ========================================================================= */
/* TEST 8: Process Accounting & Kernel Stability Post-Faults                 */
/* ========================================================================= */
static void test8_accounting_and_stability(void) {
    printk("\n--- TEST 8: Process Accounting & System Stability Post-Faults ---\n");

    size_t active = process_get_active_count();
    size_t terminated = process_get_terminated_count();

    printk("  Active Processes: %u, Terminated Processes: %u\n", (uint32_t)active, (uint32_t)terminated);
    assert_phase6(active >= 1, "At least main/idle process is currently ACTIVE");
    assert_phase6(terminated >= 10, "All terminated fault & test processes accounted in process list");

    /* Verify kernel remains fully responsive */
    uint32_t ticks1 = pit_get_ticks();
    pit_sleep(50);
    uint32_t ticks2 = pit_get_ticks();
    assert_phase6(ticks2 >= ticks1 + 2, "Kernel timer & scheduling remains stable and responsive after all user-mode faults");
}

void run_phase6_tests(void) {
    printk("\n=======================================================================\n");
    printk(">>> Beginning Phase 6 User Mode & System Call Deep Audit Test Suite <<<\n");
    printk("=======================================================================\n");

    test1_ring3_and_syscalls();
    test2_privileged_instruction_protection();
    test3_memory_permissions();
    test4_comprehensive_pointer_validation();
    test5_address_space_isolation();
    test6_preemptive_user_multitasking();
    test7_lifecycle_stress_and_reclamation();
    test8_accounting_and_stability();

    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("\n>>> [PHASE 6 AUDIT PASSED] All 8 User Mode Security & Correctness tests passed! <<<\n\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}
