#ifndef ARCH_X86_IO_H
#define ARCH_X86_IO_H

#include <kernel/types.h>

/**
 * Output a byte to the specified I/O port.
 */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/**
 * Read a byte from the specified I/O port.
 */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * Output a 16-bit word to the specified I/O port.
 */
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

/**
 * Read a 16-bit word from the specified I/O port.
 */
static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * Output a 32-bit dword to the specified I/O port.
 */
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

/**
 * Read a 32-bit dword from the specified I/O port.
 */
static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * Wait a very small amount of time for I/O operations to complete.
 * Port 0x80 is unused after POST and safely absorbs an outb delay.
 */
static inline void io_wait(void) {
    outb(0x80, 0);
}

#endif /* ARCH_X86_IO_H */
