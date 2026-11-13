#include <arch/x86/pic.h>
#include <arch/x86/io.h>

#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

void pic_init(void) {
    /* ICW1: Start initialization sequence in cascade mode */
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    /* ICW2: Remap Master PIC to vector 32 (0x20) and Slave PIC to vector 40 (0x28) */
    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();

    /* ICW3: Tell Master PIC that there is a slave PIC at IRQ2 (0000 0100) */
    outb(PIC1_DATA, 0x04);
    io_wait();
    /* Tell Slave PIC its cascade identity (0000 0010 = 2) */
    outb(PIC2_DATA, 0x02);
    io_wait();

    /* ICW4: Set 8086 mode */
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    /* Mask all interrupts initially except IRQ2 (cascade line between master and slave) */
    outb(PIC1_DATA, 0xFB);  /* 1111 1011 -> IRQ2 unmasked */
    outb(PIC2_DATA, 0xFF);  /* All slave IRQs masked */
}

void pic_set_mask(uint8_t irq_line) {
    uint16_t port;
    uint8_t value;

    if (irq_line < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq_line -= 8;
    }
    value = (uint8_t)(inb(port) | (1 << irq_line));
    outb(port, value);
}

void pic_clear_mask(uint8_t irq_line) {
    uint16_t port;
    uint8_t value;

    if (irq_line < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq_line -= 8;
    }
    value = (uint8_t)(inb(port) & ~(1 << irq_line));
    outb(port, value);
}

void pic_disable(void) {
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

static uint16_t __pic_get_irq_reg(int ocw3) {
    outb(PIC1_COMMAND, (uint8_t)ocw3);
    outb(PIC2_COMMAND, (uint8_t)ocw3);
    return (uint16_t)((inb(PIC2_COMMAND) << 8) | inb(PIC1_COMMAND));
}

uint16_t pic_get_irr(void) {
    return __pic_get_irq_reg(PIC_READ_IRR);
}

uint16_t pic_get_isr(void) {
    return __pic_get_irq_reg(PIC_READ_ISR);
}
