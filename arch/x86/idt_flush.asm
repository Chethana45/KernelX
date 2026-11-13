; ==============================================================================
; KernelX - arch/x86/idt_flush.asm
; Low-level assembly routine to load Interrupt Descriptor Table (IDT)
; ==============================================================================

section .text
global idt_flush

; ------------------------------------------------------------------------------
; idt_flush: Loads IDT pointer passed as argument on stack.
; C prototype: void idt_flush(uint32_t idt_ptr_addr);
; ------------------------------------------------------------------------------
idt_flush:
    mov eax, [esp + 4]          ; Get pointer to idt_ptr
    lidt [eax]                  ; Load IDT
    ret
