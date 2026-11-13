#include <drivers/serial.h>
#include <arch/x86/io.h>

#define SERIAL_DATA_PORT(base)          ((base) + 0)
#define SERIAL_INTERRUPT_ENABLE(base)   ((base) + 1)
#define SERIAL_FIFO_COMMAND_PORT(base)  ((base) + 2)
#define SERIAL_LINE_COMMAND_PORT(base)  ((base) + 3)
#define SERIAL_MODEM_COMMAND_PORT(base) ((base) + 4)
#define SERIAL_LINE_STATUS_PORT(base)   ((base) + 5)

int serial_init(uint16_t port) {
    outb(SERIAL_INTERRUPT_ENABLE(port), 0x00);    // Disable all interrupts
    outb(SERIAL_LINE_COMMAND_PORT(port), 0x80);    // Enable DLAB (set baud rate divisor)
    outb(SERIAL_DATA_PORT(port), 0x03);            // Set divisor to 3 (lo byte) 38400 baud
    outb(SERIAL_INTERRUPT_ENABLE(port), 0x00);    //                  (hi byte)
    outb(SERIAL_LINE_COMMAND_PORT(port), 0x03);    // 8 bits, no parity, one stop bit
    outb(SERIAL_FIFO_COMMAND_PORT(port), 0xC7);    // Enable FIFO, clear TX/RX, 14-byte threshold
    outb(SERIAL_MODEM_COMMAND_PORT(port), 0x0B);   // IRQs enabled, RTS/DSR set

    // Set in loopback mode, test the serial chip
    outb(SERIAL_MODEM_COMMAND_PORT(port), 0x1E);
    outb(SERIAL_DATA_PORT(port), 0xAE);

    // Check if serial is faulty (i.e. not returning same byte)
    if (inb(SERIAL_DATA_PORT(port)) != 0xAE) {
        return -1;
    }

    // Set normal operation mode (not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled)
    outb(SERIAL_MODEM_COMMAND_PORT(port), 0x0F);
    return 0;
}

int serial_is_transmit_empty(uint16_t port) {
    return inb(SERIAL_LINE_STATUS_PORT(port)) & 0x20;
}

void serial_putc(uint16_t port, char c) {
    while (serial_is_transmit_empty(port) == 0);
    outb(SERIAL_DATA_PORT(port), (uint8_t)c);
}

void serial_write(uint16_t port, const char* str) {
    if (!str) return;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            serial_putc(port, '\r');
        }
        serial_putc(port, str[i]);
    }
}

int serial_received(uint16_t port) {
    return inb(SERIAL_LINE_STATUS_PORT(port)) & 0x01;
}

char serial_read(uint16_t port) {
    while (serial_received(port) == 0);
    return (char)inb(SERIAL_DATA_PORT(port));
}
