#ifndef KERNEL_TYPES_H
#define KERNEL_TYPES_H

/* Standard integer types for 32-bit x86 freestanding environment */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

typedef uint32_t           size_t;
typedef int32_t            ssize_t;
typedef uint32_t           uintptr_t;
typedef int32_t            intptr_t;

#define NULL ((void*)0)

#ifndef __bool_true_false_are_defined
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
typedef enum {
    false = 0,
    true  = 1
} bool;
#define __bool_true_false_are_defined 1
#endif
#endif

#endif /* KERNEL_TYPES_H */
