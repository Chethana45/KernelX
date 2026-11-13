; ==============================================================================
; KernelX - arch/x86/interrupt.asm
; Low-level assembly interrupt service routine (ISR) & IRQ stubs
; ==============================================================================

section .text

extern isr_dispatcher
extern irq_dispatcher

; Macro for ISRs that do not push an error code
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push dword 0                ; Push dummy error code
    push dword %1               ; Push interrupt number
    jmp isr_common_stub
%endmacro

; Macro for ISRs that automatically push an error code
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push dword %1               ; Push interrupt number (error code already on stack)
    jmp isr_common_stub
%endmacro

; Macro for hardware IRQ handlers
%macro IRQ 2
global irq%1
irq%1:
    push dword 0                ; Dummy error code
    push dword %2               ; Re-mapped interrupt vector (32 + %1)
    jmp irq_common_stub
%endmacro

; CPU Exceptions (Vectors 0 - 31)
ISR_NOERRCODE 0                 ; 0: Divide by Zero
ISR_NOERRCODE 1                 ; 1: Debug
ISR_NOERRCODE 2                 ; 2: Non-Maskable Interrupt
ISR_NOERRCODE 3                 ; 3: Breakpoint
ISR_NOERRCODE 4                 ; 4: Overflow
ISR_NOERRCODE 5                 ; 5: Bound Range Exceeded
ISR_NOERRCODE 6                 ; 6: Invalid Opcode
ISR_NOERRCODE 7                 ; 7: Device Not Available
ISR_ERRCODE   8                 ; 8: Double Fault (pushes error code)
ISR_NOERRCODE 9                 ; 9: Coprocessor Segment Overrun
ISR_ERRCODE   10                ; 10: Invalid TSS (pushes error code)
ISR_ERRCODE   11                ; 11: Segment Not Present (pushes error code)
ISR_ERRCODE   12                ; 12: Stack-Segment Fault (pushes error code)
ISR_ERRCODE   13                ; 13: General Protection Fault (pushes error code)
ISR_ERRCODE   14                ; 14: Page Fault (pushes error code)
ISR_NOERRCODE 15                ; 15: Reserved
ISR_NOERRCODE 16                ; 16: x87 Floating-Point Exception
ISR_ERRCODE   17                ; 17: Alignment Check (pushes error code)
ISR_NOERRCODE 18                ; 18: Machine Check
ISR_NOERRCODE 19                ; 19: SIMD Floating-Point Exception
ISR_NOERRCODE 20                ; 20: Virtualization Exception
ISR_ERRCODE   21                ; 21: Control Protection Exception (pushes error code)
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_ERRCODE   29
ISR_ERRCODE   30
ISR_NOERRCODE 31

; Hardware IRQs (Vectors 32 - 47)
IRQ 0,  32                      ; IRQ0: Programmable Interval Timer
IRQ 1,  33                      ; IRQ1: PS/2 Keyboard
IRQ 2,  34                      ; IRQ2: Cascade for 8259A Slave
IRQ 3,  35                      ; IRQ3: Serial Port 2
IRQ 4,  36                      ; IRQ4: Serial Port 1
IRQ 5,  37                      ; IRQ5: Parallel Port 2 / Sound Card
IRQ 6,  38                      ; IRQ6: Floppy Disk Controller
IRQ 7,  39                      ; IRQ7: Parallel Port 1 / Spurious
IRQ 8,  40                      ; IRQ8: Real Time Clock
IRQ 9,  41                      ; IRQ9: ACPI / Free
IRQ 10, 42                      ; IRQ10: Free
IRQ 11, 43                      ; IRQ11: Free
IRQ 12, 44                      ; IRQ12: PS/2 Mouse
IRQ 13, 45                      ; IRQ13: FPU / Coprocessor
IRQ 14, 46                      ; IRQ14: Primary ATA Hard Disk
IRQ 15, 47                      ; IRQ15: Secondary ATA Hard Disk / Spurious

; ------------------------------------------------------------------------------
; Common stub for software exceptions (vectors 0 - 31)
; ------------------------------------------------------------------------------
isr_common_stub:
    pusha                       ; Save EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX

    xor eax, eax                ; Clear upper 16 bits of EAX
    mov ax, ds                  ; Save data segment selector (guaranteed clean 32-bit value)
    push eax

    mov ax, 0x10                ; Load kernel data segment descriptor
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                    ; Pass pointer to stack (registers_t* regs)
    call isr_dispatcher
    add esp, 4                  ; Pop registers pointer argument

    pop eax                     ; Restore original data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa                        ; Restore general purpose registers
    add esp, 8                  ; Pop error code and interrupt number
    iret                        ; Pop CS, EIP, EFLAGS (and SS, ESP if privilege level changed)

; ------------------------------------------------------------------------------
; Common stub for hardware interrupts (vectors 32 - 47)
; ------------------------------------------------------------------------------
irq_common_stub:
    pusha                       ; Save general purpose registers

    xor eax, eax                ; Clear upper 16 bits of EAX
    mov ax, ds                  ; Save data segment selector (guaranteed clean 32-bit value)
    push eax

    mov ax, 0x10                ; Load kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                    ; Pass registers_t* to irq_dispatcher
    call irq_dispatcher
    add esp, 4

    pop eax                     ; Restore original data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa
    add esp, 8                  ; Pop error code and interrupt number
    iret

; ------------------------------------------------------------------------------
; System Call Interrupt (Vector 128 / 0x80)
; ------------------------------------------------------------------------------
global isr128
isr128:
    push dword 0                ; Dummy error code
    push dword 128              ; Interrupt vector 0x80
    jmp isr_common_stub

; ------------------------------------------------------------------------------
; enter_user_mode(uint32_t user_eip, uint32_t user_esp)
; Drops CPU privilege from Ring 0 to Ring 3 and begins execution
; ------------------------------------------------------------------------------
global enter_user_mode
enter_user_mode:
    mov edx, [esp + 4]          ; User EIP
    mov ecx, [esp + 8]          ; User ESP

    ; Load User Data Segment selector (0x20 | 3 = 0x23)
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Push iret frame for Ring 3 transition:
    push dword 0x23             ; User SS
    push ecx                    ; User ESP
    push dword 0x00000202       ; EFLAGS (IF=1 interrupts enabled)
    push dword 0x1B             ; User CS (0x18 | 3 = 0x1B)
    push edx                    ; User EIP

    ; Clear general-purpose registers to avoid leaking kernel register state to user mode
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp

    iretd
