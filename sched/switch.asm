; ==============================================================================
; KernelX - sched/switch.asm
; Low-level x86 context switching routine
; ==============================================================================

section .text
global switch_to

; ------------------------------------------------------------------------------
; switch_to: Switches CPU context between two threads
; C prototype: void switch_to(context_t** prev, context_t* next);
; Stack parameters:
;   [esp + 4]: Pointer to prev thread's context* (where to save old ESP)
;   [esp + 8]: Pointer to next thread's context* (from where to load new ESP)
; ------------------------------------------------------------------------------
switch_to:
    mov eax, [esp + 4]          ; EAX = prev context**
    mov edx, [esp + 8]          ; EDX = next context*

    ; Save current CPU context (EFLAGS + callee-saved registers)
    pushfd
    push ebp
    push ebx
    push esi
    push edi

    ; Switch stack pointers
    mov [eax], esp              ; *prev = current ESP
    mov esp, edx                ; ESP = next

    ; Restore next thread's CPU context
    pop edi
    pop esi
    pop ebx
    pop ebp
    popfd                       ; Restores EFLAGS (including IF interrupt flag)

    ret                         ; Return to next thread's saved EIP
