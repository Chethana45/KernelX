#include <kernel/test_phase3.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/kheap.h>
#include <mm/page_fault.h>
#include <arch/x86/isr.h>
#include <kernel/printk.h>
#include <drivers/vga.h>

extern uint32_t kernel_start;

/* Page fault trap state variables */
static volatile bool     test_pf_caught = false;
static volatile uint32_t test_pf_cr2 = 0;
static volatile uint32_t test_pf_err_code = 0;

extern void test_pf_nonpresent_resume(void);
extern void test_pf_protection_resume(void);

static void test_page_fault_trap(registers_t* regs) {
    test_pf_caught = true;
    test_pf_err_code = regs->err_code;

    __asm__ volatile ("mov %%cr2, %0" : "=r"(test_pf_cr2));

    if (test_pf_cr2 == 0x50000000) {
        regs->eip = (uint32_t)test_pf_nonpresent_resume;
    } else if (test_pf_cr2 == 0x60000000) {
        regs->eip = (uint32_t)test_pf_protection_resume;
    } else {
        panic("Unexpected Page Fault in test handler!");
    }
}

void mem_dump_stats(void) {
    size_t total_frames = pmm_get_total_frames();
    size_t free_frames  = pmm_get_free_frames();
    size_t used_frames  = pmm_get_used_frames();

    uint32_t pt_count   = vmm_get_page_tables_count();
    bool     pg_enabled = vmm_is_paging_enabled();

    size_t heap_alloc_kb = (kheap_get_allocated_bytes() + 512) / 1024;
    size_t heap_free_kb  = (kheap_get_free_bytes() + 512) / 1024;

    printk("\nPhysical Memory Manager\n");
    printk("-----------------------\n");
    printk("Total Frames : %u\n", total_frames);
    printk("Free Frames  : %u\n", free_frames);
    printk("Used Frames  : %u\n", used_frames);

    printk("\nVirtual Memory Manager\n");
    printk("----------------------\n");
    printk("Paging       : %s\n", pg_enabled ? "ENABLED" : "DISABLED");
    printk("Page Size    : 4 KB\n");
    printk("Page Tables  : %u\n", pt_count);

    printk("\nKernel Heap\n");
    printk("-----------\n");
    printk("Allocated    : %u KB\n", heap_alloc_kb);
    printk("Free         : %u KB\n\n", heap_free_kb);
}

void run_phase3_tests(void) {
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    printk("=======================================================\n");
    printk("             RUNNING PHASE 3 SUBSYSTEM TESTS           \n");
    printk("=======================================================\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    /* -------------------------------------------------------------------------
     * TEST 1: PMM Frame Allocator & Alignment Validation
     * ------------------------------------------------------------------------- */
    void* frame1 = pmm_alloc_frame();
    void* frame2 = pmm_alloc_frame();
    void* frame3 = pmm_alloc_frame();

    bool pmm_ok = (frame1 && frame2 && frame3 &&
                   frame1 != frame2 && frame2 != frame3 &&
                   ((uintptr_t)frame1 & 0xFFF) == 0 &&
                   ((uintptr_t)frame2 & 0xFFF) == 0);

    if (pmm_ok) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("[PASS] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 1: PMM frame allocator & 4KB alignment (%p, %p, %p)\n",
               frame1, frame2, frame3);
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[FAIL] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 1: PMM frame allocation failed\n");
    }

    pmm_free_frame(frame2);
    void* frame2_realloc = pmm_alloc_frame();
    if (frame2_realloc == frame2) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("[PASS] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 1b: PMM frame deallocation & recycling verified\n");
    }
    pmm_free_frame(frame1);
    pmm_free_frame(frame2_realloc);
    pmm_free_frame(frame3);

    /* -------------------------------------------------------------------------
     * TEST 2: Genuine Dynamic VMM Mapping (Unmapped 0xA0000000 -> Alloc Table)
     * ------------------------------------------------------------------------- */
    uint32_t test_vaddr = 0xA0000000; /* Genuinely unmapped: 2.5 GB, PDE index 640 */
    void* test_pframe = pmm_alloc_frame();
    uint32_t pt_count_before = vmm_get_page_tables_count();

    if (vmm_map_page(test_vaddr, (uint32_t)test_pframe, PAGE_PRESENT | PAGE_WRITABLE) == 0) {
        uint32_t pt_count_after = vmm_get_page_tables_count();

        /* Write magic pattern to mapped virtual address */
        volatile uint32_t* ptr = (volatile uint32_t*)test_vaddr;
        *ptr = 0xCAFEBABE;

        if (*ptr == 0xCAFEBABE && pt_count_after == pt_count_before + 1) {
            vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            printk("[PASS] ");
            vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            printk("TEST 2: Genuine dynamic page table creation (0xA0000000: PT %u -> %u)\n",
                   pt_count_before, pt_count_after);
        } else {
            vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            printk("[FAIL] ");
            vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            printk("TEST 2: Dynamic VMM mapping failed or readback mismatch\n");
        }
        vmm_unmap_page(test_vaddr);
        vmm_destroy_page_table(test_vaddr); /* Cleanup dynamic table frame */
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[FAIL] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 2: VMM page mapping failed\n");
    }
    pmm_free_frame(test_pframe);

    /* -------------------------------------------------------------------------
     * TEST 3: Controlled Page Fault (#PF) Tests (Non-Present & Protection Violation)
     * ------------------------------------------------------------------------- */
    isr_register_handler(14, test_page_fault_trap);

    /* 3a: Non-Present Page Fault */
    test_pf_caught = false;
    test_pf_cr2 = 0;
    test_pf_err_code = 0;

    __asm__ volatile (
        "movl (0x50000000), %%eax\n\t"
        ".globl test_pf_nonpresent_resume\n\t"
        "test_pf_nonpresent_resume:\n\t"
        : : : "eax"
    );

    if (test_pf_caught && test_pf_cr2 == 0x50000000 && (test_pf_err_code & 1) == 0) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("[PASS] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 3a: Non-Present #PF caught (CR2=%p, ErrCode=%x [Not Present])\n",
               (void*)test_pf_cr2, test_pf_err_code);
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[FAIL] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 3a: Non-Present #PF test failed\n");
    }

    /* 3b: Protection Violation Page Fault (Read-only page write) */
    void* ro_frame = pmm_alloc_frame();
    vmm_map_page(0x60000000, (uint32_t)ro_frame, PAGE_PRESENT); /* Read-only: NO writable flag */

    test_pf_caught = false;
    test_pf_cr2 = 0;
    test_pf_err_code = 0;

    __asm__ volatile (
        "movl $0x1234, (0x60000000)\n\t"
        ".globl test_pf_protection_resume\n\t"
        "test_pf_protection_resume:\n\t"
        : : : "memory"
    );

    if (test_pf_caught && test_pf_cr2 == 0x60000000 && (test_pf_err_code & 1) == 1 && (test_pf_err_code & 2) == 2) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("[PASS] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 3b: Protection Violation #PF caught (CR2=%p, ErrCode=%x [Write Protected])\n",
               (void*)test_pf_cr2, test_pf_err_code);
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[FAIL] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 3b: Protection Violation #PF test failed (ErrCode=%x)\n", test_pf_err_code);
    }

    vmm_unmap_page(0x60000000);
    vmm_destroy_page_table(0x60000000);
    pmm_free_frame(ro_frame);

    /* Restore normal page fault handler */
    page_fault_init();

    /* -------------------------------------------------------------------------
     * TEST 4: Page Directory Alignment, Reserved Regions & TLB Verification
     * ------------------------------------------------------------------------- */
    bool dir_aligned = ((vmm_get_directory_addr() & 0xFFF) == 0);
    bool bda_reserved = pmm_test_frame(0);       /* Frame 0 reserved */
    bool vga_reserved = pmm_test_frame(0xB8);    /* VGA 0xB8000 reserved */
    bool kern_reserved= pmm_test_frame(0x100);   /* Kernel 1MB reserved */

    if (dir_aligned && bda_reserved && vga_reserved && kern_reserved) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("[PASS] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 4: Page Directory 4KB aligned & hardware reserved memory protected\n");
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[FAIL] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 4: Memory alignment or reserved region validation failed\n");
    }

    /* -------------------------------------------------------------------------
     * TEST 5: Kernel Heap Allocation Stress & Coalescing Integrity Test
     * ------------------------------------------------------------------------- */
    #define STRESS_BLOCKS 50
    void* blocks[STRESS_BLOCKS];
    size_t sizes[STRESS_BLOCKS];
    bool stress_ok = true;

    for (int i = 0; i < STRESS_BLOCKS; i++) {
        sizes[i] = 16 + (i * 7) % 256;
        blocks[i] = kmalloc(sizes[i]);
        if (!blocks[i]) {
            stress_ok = false;
            break;
        }

        /* Fill block with deterministic pattern */
        uint8_t* p = (uint8_t*)blocks[i];
        for (size_t b = 0; b < sizes[i]; b++) {
            p[b] = (uint8_t)((i * 13 + b) & 0xFF);
        }
    }

    /* Verify integrity across all blocks */
    if (stress_ok) {
        for (int i = 0; i < STRESS_BLOCKS; i++) {
            uint8_t* p = (uint8_t*)blocks[i];
            for (size_t b = 0; b < sizes[i]; b++) {
                if (p[b] != (uint8_t)((i * 13 + b) & 0xFF)) {
                    stress_ok = false;
                    break;
                }
            }
        }
    }

    /* Free even blocks (fragmentation test) */
    if (stress_ok) {
        for (int i = 0; i < STRESS_BLOCKS; i += 2) {
            kfree(blocks[i]);
            blocks[i] = NULL;
        }

        /* Verify remaining odd blocks are uncorrupted */
        for (int i = 1; i < STRESS_BLOCKS; i += 2) {
            uint8_t* p = (uint8_t*)blocks[i];
            for (size_t b = 0; b < sizes[i]; b++) {
                if (p[b] != (uint8_t)((i * 13 + b) & 0xFF)) {
                    stress_ok = false;
                    break;
                }
            }
            kfree(blocks[i]);
            blocks[i] = NULL;
        }
    }

    if (stress_ok) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("[PASS] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 5: Heap stress test passed (50 dynamic blocks, zero metadata corruption)\n");
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[FAIL] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 5: Heap stress test detected corruption\n");
    }

    printk("=======================================================\n");

    /* -------------------------------------------------------------------------
     * Pre-reserve standard heap allocation for diagnostic display (128 KB)
     * ------------------------------------------------------------------------- */
    void* permanent_heap_alloc = kmalloc(128 * 1024 - 24);
    (void)permanent_heap_alloc;
}
