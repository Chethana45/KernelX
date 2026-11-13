#ifndef ARCH_X86_PIC_H
#define ARCH_X86_PIC_H

#include <kernel/types.h>

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define PIC_EOI      0x20

#define PIC_READ_IRR 0x0A    /* OCW3 irq ready next CMD read */
#define PIC_READ_ISR 0x0B    /* OCW3 irq service next CMD read */

void pic_init(void);
void pic_send_eoi(uint8_t irq);
void pic_set_mask(uint8_t irq_line);
void pic_clear_mask(uint8_t irq_line);
void pic_disable(void);
uint16_t pic_get_irr(void);
uint16_t pic_get_isr(void);

#endif /* ARCH_X86_PIC_H */
