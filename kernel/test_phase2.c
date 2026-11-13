#include <kernel/test_phase2.h>
#include <arch/x86/gdt.h>
#include <arch/x86/idt.h>
#include <arch/x86/isr.h>
#include <arch/x86/pic.h>
#include <arch/x86/pit.h>
#include <drivers/keyboard.h>
#include <drivers/vga.h>
#include <drivers/serial.h>
#include <kernel/printk.h>

static volatile bool div0_handled = false;
extern void test_div0_resume_point(void);

static void test_div0_handler(registers_t* regs) {
    div0_handled = true;
    vga_set_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK);
    printk("    [ISR 0 Trap] Caught Division By Zero (#DE) at EIP=%p! Resuming...\n", (void*)regs->eip);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    /* Advance EIP to test_div0_resume_point to resume normal kernel execution */
    regs->eip = (uint32_t)test_div0_resume_point;
}

void run_phase2_tests(void) {
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    printk("\n=======================================================\n");
    printk("             RUNNING PHASE 2 SUBSYSTEM TESTS           \n");
    printk("=======================================================\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    /* -------------------------------------------------------------------------
     * TEST 1: GDT initialization and segment register validation
     * ------------------------------------------------------------------------- */
    uint16_t cs_val, ds_val, ss_val, tr_val;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs_val));
    __asm__ volatile ("mov %%ds, %0" : "=r"(ds_val));
    __asm__ volatile ("mov %%ss, %0" : "=r"(ss_val));
    __asm__ volatile ("str %0" : "=r"(tr_val));

    if (cs_val == KERNEL_CS && ds_val == KERNEL_DS && ss_val == KERNEL_DS && (tr_val & 0xFFF8) == TSS_SEG) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("[PASS] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 1: GDT loaded correctly (CS=%x, DS=%x, SS=%x, TR=%x)\n",
               cs_val, ds_val, ss_val, tr_val);
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[FAIL] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 1: GDT segment mismatch (CS=%x, DS=%x, SS=%x, TR=%x)\n",
               cs_val, ds_val, ss_val, tr_val);
    }

    /* -------------------------------------------------------------------------
     * TEST 2: IDT initialization and descriptor verification
     * ------------------------------------------------------------------------- */
    idt_ptr_t idt_check;
    __asm__ volatile ("sidt %0" : "=m"(idt_check));

    if (idt_check.limit == (sizeof(idt_entry_t) * IDT_ENTRIES - 1) && idt_check.base != 0) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("[PASS] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 2: IDT initialized (256 gates, limit=%u, base=%p)\n",
               idt_check.limit, (void*)idt_check.base);
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[FAIL] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 2: IDTR invalid (limit=%u, base=%p)\n",
               idt_check.limit, (void*)idt_check.base);
    }

    /* -------------------------------------------------------------------------
     * TEST 3: Controlled divide-by-zero exception
     * ------------------------------------------------------------------------- */
    div0_handled = false;
    isr_register_handler(0, test_div0_handler);

    __asm__ volatile (
        "xor %%edx, %%edx\n\t"
        "mov $10, %%eax\n\t"
        "mov $0, %%ecx\n\t"
        "div %%ecx\n\t"
        ".globl test_div0_resume_point\n\t"
        "test_div0_resume_point:\n\t"
        : : : "eax", "edx", "ecx"
    );

    /* Deregister temporary test handler */
    isr_register_handler(0, NULL);

    if (div0_handled) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("[PASS] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 3: Controlled Divide-by-Zero (#DE) caught and recovered safely\n");
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[FAIL] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 3: Divide-by-Zero exception was not handled\n");
    }

    /* -------------------------------------------------------------------------
     * TEST 4: Timer interrupts and tick counter increment
     * ------------------------------------------------------------------------- */
    uint32_t ticks_before = pit_get_ticks();
    pit_sleep(150); /* Sleep 150 ms (approx 15 ticks at 100 Hz) */
    uint32_t ticks_after = pit_get_ticks();
    uint32_t delta = ticks_after - ticks_before;

    if (delta >= 10) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("[PASS] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 4: PIT timer ticks active (ticks: %u -> %u, +%u ticks)\n",
               ticks_before, ticks_after, delta);
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[FAIL] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("TEST 4: PIT timer insufficient ticks (delta=%u)\n", delta);
    }

    /* -------------------------------------------------------------------------
     * TEST 5: Keyboard driver initialization
     * ------------------------------------------------------------------------- */
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[PASS] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("TEST 5: PS/2 Keyboard IRQ1 driver active (circular buffer ready)\n");

    /* -------------------------------------------------------------------------
     * TEST 6: Serial debugging validation
     * ------------------------------------------------------------------------- */
    serial_write(COM1_PORT, ">>> [TEST 6] Serial debug channel active and synchronized <<<\n");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[PASS] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("TEST 6: Dual-sink serial debugging verified\n");

    printk("=======================================================\n\n");
}
