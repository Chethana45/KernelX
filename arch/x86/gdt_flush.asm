; ==============================================================================
; KernelX - arch/x86/gdt_flush.asm
; Low-level assembly routines to load GDT and Task State Segment (TSS)
; ==============================================================================

section .text
global gdt_flush
global tss_flush

; ------------------------------------------------------------------------------
; gdt_flush: Loads GDT pointer passed on stack, performs far jump to reload CS,
;            and reloads all data segment registers with 0x10.
; C prototype: void gdt_flush(uint32_t gdt_ptr_addr);
; ------------------------------------------------------------------------------
gdt_flush:
    mov eax, [esp + 4]          ; Get pointer to gdt_ptr
    lgdt [eax]                  ; Load GDT

    ; Reload data segment selectors with Kernel Data Segment (0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Perform far jump to reload Code Segment (CS) with Kernel Code Segment (0x08)
    jmp 0x08:.flush

.flush:
    ret

; ------------------------------------------------------------------------------
; tss_flush: Loads Task Register (TR) with TSS segment selector (0x28).
; C prototype: void tss_flush(void);
; ------------------------------------------------------------------------------
tss_flush:
    mov ax, 0x28 | 0            ; Selector 0x28 (index 5, RPL 0)
    ltr ax                      ; Load Task Register
    ret
