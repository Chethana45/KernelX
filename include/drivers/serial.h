#ifndef DRIVERS_SERIAL_H
#define DRIVERS_SERIAL_H

#include <kernel/types.h>

#define COM1_PORT 0x3F8

int  serial_init(uint16_t port);
int  serial_is_transmit_empty(uint16_t port);
void serial_putc(uint16_t port, char c);
void serial_write(uint16_t port, const char* str);
char serial_read(uint16_t port);
int  serial_received(uint16_t port);

#endif /* DRIVERS_SERIAL_H */
