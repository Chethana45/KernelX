#ifndef ARCH_X86_ISR_H
#define ARCH_X86_ISR_H

#include <kernel/types.h>

/* Registers pushed onto the stack by interrupt.asm stubs */
typedef struct registers {
    uint32_t ds;                                      /* Data segment selector */
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; /* Pushed by pusha */
    uint32_t int_no, err_code;                        /* Interrupt number and error code */
    uint32_t eip, cs, eflags, useresp, ss;            /* Pushed by CPU automatically */
} registers_t;

typedef void (*isr_handler_t)(registers_t* regs);

#define IRQ0  32
#define IRQ1  33
#define IRQ2  34
#define IRQ3  35
#define IRQ4  36
#define IRQ5  37
#define IRQ6  38
#define IRQ7  39
#define IRQ8  40
#define IRQ9  41
#define IRQ10 42
#define IRQ11 43
#define IRQ12 44
#define IRQ13 45
#define IRQ14 46
#define IRQ15 47

void isr_init(void);
void isr_register_handler(uint8_t n, isr_handler_t handler);
void isr_dispatcher(registers_t* regs);
void irq_dispatcher(registers_t* regs);

#endif /* ARCH_X86_ISR_H */
