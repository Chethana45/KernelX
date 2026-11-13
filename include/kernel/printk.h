#ifndef KERNEL_PRINTK_H
#define KERNEL_PRINTK_H

#include <kernel/types.h>

/* Built-in va_list support for freestanding environment */
typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, l)   __builtin_va_arg(v, l)

void printk(const char* fmt, ...);
void vprintk(const char* fmt, va_list args);
void panic(const char* msg) __attribute__((noreturn));

#endif /* KERNEL_PRINTK_H */
