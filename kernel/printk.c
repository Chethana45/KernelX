#include <kernel/printk.h>
#include <drivers/vga.h>
#include <drivers/serial.h>

static void print_char(char c) {
    vga_putc(c);
    if (c == '\n') {
        serial_putc(COM1_PORT, '\r');
    }
    serial_putc(COM1_PORT, c);
}

static void print_string(const char* str) {
    if (!str) {
        str = "(null)";
    }
    for (size_t i = 0; str[i] != '\0'; i++) {
        print_char(str[i]);
    }
}

static void print_uint(uint32_t n, uint32_t base, bool uppercase) {
    char buf[36];
    const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;

    if (n == 0) {
        print_char('0');
        return;
    }

    while (n > 0) {
        buf[i++] = digits[n % base];
        n /= base;
    }

    for (int j = i - 1; j >= 0; j--) {
        print_char(buf[j]);
    }
}

static void print_int(int32_t n) {
    if (n < 0) {
        print_char('-');
        /* Handle INT32_MIN overflow edge case */
        if (n == (int32_t)0x80000000) {
            print_string("2147483648");
            return;
        }
        n = -n;
    }
    print_uint((uint32_t)n, 10, false);
}

void vprintk(const char* fmt, va_list args) {
    for (size_t i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] != '%') {
            print_char(fmt[i]);
            continue;
        }

        i++; // skip '%'
        if (fmt[i] == '\0') break;

        switch (fmt[i]) {
            case 'c': {
                char c = (char)va_arg(args, int);
                print_char(c);
                break;
            }
            case 's': {
                const char* str = va_arg(args, const char*);
                print_string(str);
                break;
            }
            case 'd':
            case 'i': {
                int32_t val = va_arg(args, int32_t);
                print_int(val);
                break;
            }
            case 'u': {
                uint32_t val = va_arg(args, uint32_t);
                print_uint(val, 10, false);
                break;
            }
            case 'x': {
                uint32_t val = va_arg(args, uint32_t);
                print_string("0x");
                print_uint(val, 16, false);
                break;
            }
            case 'X': {
                uint32_t val = va_arg(args, uint32_t);
                print_string("0x");
                print_uint(val, 16, true);
                break;
            }
            case 'p': {
                uintptr_t val = (uintptr_t)va_arg(args, void*);
                print_string("0x");
                print_uint((uint32_t)val, 16, false);
                break;
            }
            case '%': {
                print_char('%');
                break;
            }
            default: {
                print_char('%');
                print_char(fmt[i]);
                break;
            }
        }
    }
}

void printk(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);
}

void panic(const char* msg) {
    __asm__ volatile ("cli");

    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    printk("\n=======================================================\n");
    printk("                   KERNEL PANIC                        \n");
    printk("=======================================================\n");
    printk("FATAL: %s\n", msg ? msg : "Unspecified system panic");
    printk("System execution halted to prevent corruption.\n");
    printk("=======================================================\n");

    while (1) {
        __asm__ volatile ("hlt");
    }
}
