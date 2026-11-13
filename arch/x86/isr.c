#include <arch/x86/isr.h>
#include <arch/x86/pic.h>
#include <sched/process.h>
#include <kernel/printk.h>
#include <drivers/vga.h>

static isr_handler_t interrupt_handlers[256];

static const char* const exception_messages[] = {
    "Division By Zero (#DE)",
    "Debug (#DB)",
    "Non-Maskable Interrupt (#NMI)",
    "Breakpoint (#BP)",
    "Into Detected Overflow (#OF)",
    "Out of Bounds (#BR)",
    "Invalid Opcode (#UD)",
    "No Coprocessor / Device Not Available (#NM)",
    "Double Fault (#DF)",
    "Coprocessor Segment Overrun",
    "Bad TSS (#TS)",
    "Segment Not Present (#NP)",
    "Stack-Segment Fault (#SS)",
    "General Protection Fault (#GP)",
    "Page Fault (#PF)",
    "Unknown Interrupt / Reserved",
    "x87 FPU Floating-Point Error (#MF)",
    "Alignment Check (#AC)",
    "Machine Check (#MC)",
    "SIMD Floating-Point Exception (#XM/#XF)",
    "Virtualization Exception (#VE)",
    "Control Protection Exception (#CP)",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection Exception (#HV)",
    "VMM Communication Exception (#VC)",
    "Security Exception (#SX)",
    "Reserved"
};

void isr_init(void) {
    for (size_t i = 0; i < 256; i++) {
        interrupt_handlers[i] = NULL;
    }
}

void isr_register_handler(uint8_t n, isr_handler_t handler) {
    interrupt_handlers[n] = handler;
}

static inline uint32_t read_cr2(void) {
    uint32_t val;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(val));
    return val;
}

void isr_dispatcher(registers_t* regs) {
    if (interrupt_handlers[regs->int_no] != NULL) {
        isr_handler_t handler = interrupt_handlers[regs->int_no];
        handler(regs);
        return;
    }

    /* Unhandled Exception: Dump diagnostic state */
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    printk("\n=======================================================\n");
    printk("               UNHANDLED CPU EXCEPTION                 \n");
    printk("=======================================================\n");

    const char* name = "Unknown Exception";
    if (regs->int_no < 32) {
        name = exception_messages[regs->int_no];
    }
    printk("Vector: %d [%s] | Error Code: %x\n\n", regs->int_no, name, regs->err_code);

    printk("Registers Dump:\n");
    printk("  EIP:    %p   CS:  %x   EFLAGS: %p\n", (void*)regs->eip, regs->cs, (void*)regs->eflags);
    printk("  EAX:    %p   EBX: %p   ECX:    %p   EDX: %p\n",
           (void*)regs->eax, (void*)regs->ebx, (void*)regs->ecx, (void*)regs->edx);
    printk("  ESI:    %p   EDI: %p   EBP:    %p   ESP: %p\n",
           (void*)regs->esi, (void*)regs->edi, (void*)regs->ebp, (void*)regs->esp);
    printk("  DS:     %x\n", regs->ds);

    if (regs->int_no == 14) {
        uint32_t cr2 = read_cr2();
        printk("  CR2 (Faulting Address): %p\n", (void*)cr2);
    }

    printk("=======================================================\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    /* If exception occurred in User Mode (Ring 3), terminate the process rather than halting the OS */
    if ((regs->cs & 3) == 3) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[Security] Ring 3 Exception %d [%s]: Offending process terminated.\n\n",
               regs->int_no, name);
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        thread_exit();
        return;
    }

    panic("Kernel execution halted due to unhandled processor exception.");
}

void irq_dispatcher(registers_t* regs) {
    uint8_t irq = (uint8_t)(regs->int_no - 32);

    /* Handle spurious IRQ7 (Master PIC) */
    if (irq == 7) {
        if ((pic_get_isr() & (1 << 7)) == 0) {
            /* Spurious interrupt, ignore without sending EOI */
            return;
        }
    }

    /* Handle spurious IRQ15 (Slave PIC) */
    if (irq == 15) {
        if ((pic_get_isr() & (1 << 15)) == 0) {
            /* Spurious interrupt on slave, must send EOI to master only */
            pic_send_eoi(7);
            return;
        }
    }

    /* Acknowledge interrupt to PIC before invoking handlers so context switching is safe */
    pic_send_eoi(irq);

    /* Execute registered IRQ handler */
    if (interrupt_handlers[regs->int_no] != NULL) {
        isr_handler_t handler = interrupt_handlers[regs->int_no];
        handler(regs);
    }
}
