/* Linker script for KernelX (x86 32-bit ELF) */
ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)

SECTIONS
{
    /* Load kernel at 1MB physical address (convention for x86 kernels) */
    . = 1M;

    kernel_start = .;

    /* Multiboot header must appear within the first 8192 bytes */
    .text BLOCK(4K) : ALIGN(4K)
    {
        *(.multiboot)
        *(.text)
        *(.text.*)
    }

    /* Read-only data */
    .rodata BLOCK(4K) : ALIGN(4K)
    {
        *(.rodata)
        *(.rodata.*)
    }

    /* Read-write data (initialized) */
    .data BLOCK(4K) : ALIGN(4K)
    {
        *(.data)
        *(.data.*)
    }

    /* Read-write data (uninitialized) and stack */
    .bss BLOCK(4K) : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    kernel_end = .;
}
