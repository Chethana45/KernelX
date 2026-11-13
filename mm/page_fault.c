#include <mm/page_fault.h>
#include <arch/x86/isr.h>
#include <sched/process.h>
#include <kernel/printk.h>
#include <drivers/vga.h>

static page_fault_hook_t active_pf_hook = NULL;

void page_fault_set_hook(page_fault_hook_t hook) {
    active_pf_hook = hook;
}

void page_fault_handler(registers_t* regs) {
    uint32_t faulting_address;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(faulting_address));

    /* Check if a custom subsystem/test hook handles this fault */
    if (active_pf_hook != NULL) {
        if (active_pf_hook(regs, faulting_address)) {
            return;
        }
    }

    int present   = regs->err_code & 0x1;  /* Page not present vs protection */
    int write     = regs->err_code & 0x2;  /* Write operation vs read */
    int user      = regs->err_code & 0x4;  /* Processor was in user-mode */
    int reserved  = regs->err_code & 0x8;  /* Overwritten CPU-reserved bits */
    int inst_fetch= regs->err_code & 0x10; /* Caused by instruction fetch */

    /* If the fault occurred in Ring 3 user space, safely terminate the offending process */
    if (user) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("\n[Process Terminated] User-mode Page Fault: addr=%p eip=%p [%s, %s]\n",
               (void*)faulting_address, (void*)regs->eip,
               present ? "Protection Violation" : "Non-Present",
               write ? "Write" : "Read");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

        thread_exit();
        return;
    }

    /* Kernel mode unhandled page fault -> Fatal Panic */
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    printk("\n=======================================================\n");
    printk("               KERNEL PAGE FAULT (#PF)                 \n");
    printk("=======================================================\n");
    printk("Faulting Address (CR2): %p\n", (void*)faulting_address);
    printk("Instruction Pointer (EIP): %p\n", (void*)regs->eip);
    printk("Error Code: %x [ %s | %s | %s | %s | %s ]\n",
           regs->err_code,
           present ? "Protection Violation" : "Page Not Present",
           write ? "Write Operation" : "Read Operation",
           user ? "User Mode (Ring 3)" : "Kernel Mode (Ring 0)",
           reserved ? "Reserved Bits Overwritten" : "Valid Bits",
           inst_fetch ? "Instruction Fetch" : "Data Access");
    printk("=======================================================\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    panic("Kernel execution halted due to unhandled Page Fault.");
}

void page_fault_init(void) {
    active_pf_hook = NULL;
    isr_register_handler(14, page_fault_handler);
}
