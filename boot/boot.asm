; ==============================================================================
; KernelX - boot/boot.asm
; Multiboot 1 standard bootloader stub & stack setup
; ==============================================================================

MBALIGN     equ  1 << 0             ; Align loaded modules on page boundaries
MEMINFO     equ  1 << 1             ; Provide memory map
FLAGS       equ  MBALIGN | MEMINFO  ; Multiboot 'flag' field
MAGIC       equ  0x1BADB002         ; 'magic number' lets bootloader find the header
CHECKSUM    equ -(MAGIC + FLAGS)    ; Checksum required: MAGIC + FLAGS + CHECKSUM = 0

; ------------------------------------------------------------------------------
; Multiboot Header Section
; Must be located within the first 8192 bytes of the OS image.
; ------------------------------------------------------------------------------
section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

; ------------------------------------------------------------------------------
; Kernel Stack Allocation
; 16 KB kernel bootstrap stack in BSS
; ------------------------------------------------------------------------------
section .bss
align 16
stack_bottom:
    resb 16384                      ; 16 KB kernel stack
stack_top:

; ------------------------------------------------------------------------------
; Kernel Entry Point
; ------------------------------------------------------------------------------
section .text
global _start
extern kmain

_start:
    ; Disable interrupts immediately until GDT & IDT are initialized
    cli
    cld                             ; Clear direction flag (standard for C ABI)

    ; Initialize stack pointer
    mov esp, stack_top

    ; Reset EFLAGS
    push dword 0
    popf

    ; Push Multiboot parameters according to C calling convention (cdecl)
    ; kmain(uint32_t magic, multiboot_info_t* mbd)
    push ebx                        ; Argument 2: Pointer to Multiboot structure
    push eax                        ; Argument 1: Multiboot magic value

    ; Call the high-level C kernel entry point
    call kmain

    ; If kmain unexpectedly returns, freeze CPU safely
.halt:
    cli
    hlt
    jmp .halt
