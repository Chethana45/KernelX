; ==============================================================================
; KernelX - arch/x86/user_tests.asm
; Genuine x86 Ring 3 user mode test programs
; ==============================================================================

section .text

; ------------------------------------------------------------------------------
; 1. user_prog_basic:
;    Verifies Ring 3 CS privilege, tests SYS_GETPID, SYS_WRITE, SYS_YIELD, SYS_EXIT
; ------------------------------------------------------------------------------
global user_prog_basic
global user_prog_basic_size
user_prog_basic:
    ; 1. Check CS privilege level: CS & 3 must equal 3 (Ring 3)
    mov ax, cs
    and ax, 3
    cmp ax, 3
    jne .fail_cs

    ; 2. Invoke SYS_GETPID (5)
    mov eax, 5
    int 0x80
    cmp eax, 0
    jle .fail_pid

    ; 3. Invoke SYS_WRITE (3): write message to stdout (1)
    call .get_msg
.msg:
    db "  [User Ring 3] Execution verified! Testing int 0x80 syscalls...", 10, 0
.msg_end:

.get_msg:
    pop ecx                     ; ECX = address of .msg
    mov eax, 3                  ; SYS_WRITE
    mov ebx, 1                  ; stdout
    mov edx, (.msg_end - .msg)  ; count
    int 0x80
    cmp eax, (.msg_end - .msg)
    jne .fail_write

    ; 4. Invoke SYS_YIELD (2)
    mov eax, 2
    int 0x80
    cmp eax, 0
    jne .fail_yield

    ; 5. Invoke SYS_EXIT (1) with success code 42
    mov eax, 1
    mov ebx, 42
    int 0x80

.fail_cs:
    mov eax, 1
    mov ebx, 1
    int 0x80

.fail_pid:
    mov eax, 1
    mov ebx, 2
    int 0x80

.fail_write:
    mov eax, 1
    mov ebx, 3
    int 0x80

.fail_yield:
    mov eax, 1
    mov ebx, 4
    int 0x80

user_prog_basic_end:
user_prog_basic_size: dd (user_prog_basic_end - user_prog_basic)

; ------------------------------------------------------------------------------
; 2. user_prog_priv_violation:
;    Attempts to execute privileged instruction 'cli' in Ring 3 -> triggers #GP
; ------------------------------------------------------------------------------
global user_prog_priv_violation
global user_prog_priv_violation_size
user_prog_priv_violation:
    cli                         ; Privileged instruction: MUST trigger #GP (vector 13)!
    ; If CPU improperly allowed cli, exit with error 99
    mov eax, 1
    mov ebx, 99
    int 0x80
user_prog_priv_violation_end:
user_prog_priv_violation_size: dd (user_prog_priv_violation_end - user_prog_priv_violation)

; ------------------------------------------------------------------------------
; 3. user_prog_read_kernel_mem:
;    Attempts to read kernel memory at 0x00100000 -> triggers #PF (vector 14)
; ------------------------------------------------------------------------------
global user_prog_read_kernel_mem
global user_prog_read_kernel_mem_size
global user_prog_kernel_mem_violation
global user_prog_kernel_mem_violation_size
user_prog_read_kernel_mem:
user_prog_kernel_mem_violation:
    mov edx, 0x00100000         ; Kernel load address (Supervisor-only page)
    mov eax, [edx]              ; MUST trigger Page Fault #PF (vector 14)!
    mov eax, 1
    mov ebx, 99
    int 0x80
user_prog_read_kernel_mem_end:
user_prog_kernel_mem_violation_end:
user_prog_read_kernel_mem_size: dd (user_prog_read_kernel_mem_end - user_prog_read_kernel_mem)
user_prog_kernel_mem_violation_size: dd (user_prog_kernel_mem_violation_end - user_prog_kernel_mem_violation)

; ------------------------------------------------------------------------------
; 3b. user_prog_write_kernel_mem:
;     Attempts to write to kernel memory at 0x00100000 -> triggers #PF
; ------------------------------------------------------------------------------
global user_prog_write_kernel_mem
global user_prog_write_kernel_mem_size
user_prog_write_kernel_mem:
    mov edx, 0x00100000         ; Kernel address (Supervisor page)
    mov dword [edx], 0x12345678 ; MUST trigger #PF protection fault!
    mov eax, 1
    mov ebx, 99
    int 0x80
user_prog_write_kernel_mem_end:
user_prog_write_kernel_mem_size: dd (user_prog_write_kernel_mem_end - user_prog_write_kernel_mem)

; ------------------------------------------------------------------------------
; 3c. user_prog_write_code:
;     Attempts to write to read-only user code page at 0x40000000 -> triggers #PF
; ------------------------------------------------------------------------------
global user_prog_write_code
global user_prog_write_code_size
user_prog_write_code:
    mov edx, 0x40000000         ; Read-only user code page
    mov dword [edx], 0x90909090 ; MUST trigger #PF write-protection fault!
    mov eax, 1
    mov ebx, 99
    int 0x80
user_prog_write_code_end:
user_prog_write_code_size: dd (user_prog_write_code_end - user_prog_write_code)

; ------------------------------------------------------------------------------
; 3d. user_prog_read_unmapped:
;     Attempts to read unmapped page at 0x50000000 -> triggers #PF (non-present)
; ------------------------------------------------------------------------------
global user_prog_read_unmapped
global user_prog_read_unmapped_size
user_prog_read_unmapped:
    mov edx, 0x50000000         ; Completely unmapped virtual address
    mov eax, [edx]              ; MUST trigger #PF non-present fault!
    mov eax, 1
    mov ebx, 99
    int 0x80
user_prog_read_unmapped_end:
user_prog_read_unmapped_size: dd (user_prog_read_unmapped_end - user_prog_read_unmapped)

; ------------------------------------------------------------------------------
; 4. user_prog_syscall_edge_cases:
;    Comprehensive validation of syscall pointers, range overflow, cross-page,
;    read-only targets, NULL pointers, and zero-length buffers
; ------------------------------------------------------------------------------
global user_prog_invalid_ptr_syscall
global user_prog_invalid_ptr_syscall_size
global user_prog_syscall_edge_cases
global user_prog_syscall_edge_cases_size
user_prog_invalid_ptr_syscall:
user_prog_syscall_edge_cases:
    ; Test 1: Kernel space pointer (0x00100000) -> expects -14 (-EFAULT)
    mov eax, 3                  ; SYS_WRITE
    mov ebx, 1                  ; stdout
    mov ecx, 0x00100000         ; Invalid pointer: kernel memory
    mov edx, 32                 ; count
    int 0x80
    cmp eax, -14
    jne .fail_1

    ; Test 2: Address range overflow (0xFFFFFFF0 + 32) -> expects -14
    mov eax, 3
    mov ebx, 1
    mov ecx, 0xFFFFFFF0
    mov edx, 32
    int 0x80
    cmp eax, -14
    jne .fail_2

    ; Test 3: Buffer crossing into unmapped page boundary
    ; 0x40000F00 is in valid code page, but +512 bytes reaches 0x40001100 (unmapped) -> expects -14
    mov eax, 3
    mov ebx, 1
    mov ecx, 0x40000F00
    mov edx, 512
    int 0x80
    cmp eax, -14
    jne .fail_3

    ; Test 4: Write-intent into Read-Only user page
    ; SYS_READ with buf pointing to read-only code page 0x40000000 -> expects -14
    mov eax, 4                  ; SYS_READ
    mov ebx, 0                  ; stdin
    mov ecx, 0x40000000         ; Read-only user code address
    mov edx, 16                 ; count
    int 0x80
    cmp eax, -14
    jne .fail_4

    ; Test 5: NULL pointer (0x00000000) -> expects -14
    mov eax, 3
    mov ebx, 1
    xor ecx, ecx                ; NULL
    mov edx, 16
    int 0x80
    cmp eax, -14
    jne .fail_5

    ; Test 6: Zero-length buffer with valid user pointer -> expects 0
    mov eax, 3
    mov ebx, 1
    mov ecx, 0x40002000         ; Valid user data page
    xor edx, edx                ; count = 0
    int 0x80
    cmp eax, 0
    jne .fail_6

    ; All syscall tests passed! Exit with success code 42
    mov eax, 1
    mov ebx, 42
    int 0x80

.fail_1:
    mov eax, 1
    mov ebx, 101
    int 0x80
.fail_2:
    mov eax, 1
    mov ebx, 102
    int 0x80
.fail_3:
    mov eax, 1
    mov ebx, 103
    int 0x80
.fail_4:
    mov eax, 1
    mov ebx, 104
    int 0x80
.fail_5:
    mov eax, 1
    mov ebx, 105
    int 0x80
.fail_6:
    mov eax, 1
    mov ebx, 106
    int 0x80

user_prog_syscall_edge_cases_end:
user_prog_invalid_ptr_syscall_end:
user_prog_invalid_ptr_syscall_size: dd (user_prog_invalid_ptr_syscall_end - user_prog_invalid_ptr_syscall)
user_prog_syscall_edge_cases_size:  dd (user_prog_syscall_edge_cases_end - user_prog_syscall_edge_cases)

; ------------------------------------------------------------------------------
; 5. user_prog_isolation_a:
;    Writes 0x11223344 to private data page [0x40002000], yields, verifies intact
; ------------------------------------------------------------------------------
global user_prog_isolation_a
global user_prog_isolation_a_size
user_prog_isolation_a:
    mov edx, 0x40002000
    mov dword [edx], 0x11223344

    ; Yield 5 times to let Process B execute
    mov ecx, 5
.loop_yield:
    push ecx
    mov eax, 2                  ; SYS_YIELD
    int 0x80
    pop ecx
    loop .loop_yield

    ; Verify that Process B did NOT overwrite our private memory
    mov edx, 0x40002000
    cmp dword [edx], 0x11223344
    jne .corrupted

    mov eax, 1
    mov ebx, 42
    int 0x80

.corrupted:
    mov eax, 1
    mov ebx, 99
    int 0x80
user_prog_isolation_a_end:
user_prog_isolation_a_size: dd (user_prog_isolation_a_end - user_prog_isolation_a)

; ------------------------------------------------------------------------------
; 6. user_prog_isolation_b:
;    Writes 0x55667788 to private data page [0x40002000], yields, verifies intact
; ------------------------------------------------------------------------------
global user_prog_isolation_b
global user_prog_isolation_b_size
user_prog_isolation_b:
    mov edx, 0x40002000
    mov dword [edx], 0x55667788

    mov ecx, 5
.loop_yield:
    push ecx
    mov eax, 2                  ; SYS_YIELD
    int 0x80
    pop ecx
    loop .loop_yield

    mov edx, 0x40002000
    cmp dword [edx], 0x55667788
    jne .corrupted

    mov eax, 1
    mov ebx, 42
    int 0x80

.corrupted:
    mov eax, 1
    mov ebx, 99
    int 0x80
user_prog_isolation_b_end:
user_prog_isolation_b_size: dd (user_prog_isolation_b_end - user_prog_isolation_b)

; ------------------------------------------------------------------------------
; 7. user_prog_preempt_a & b:
;    Compute loop tests preemptive multitasking in Ring 3
; ------------------------------------------------------------------------------
global user_prog_preempt_a
global user_prog_preempt_a_size
user_prog_preempt_a:
    mov ecx, 5000000
.spin:
    dec ecx
    jnz .spin

    mov eax, 1
    mov ebx, 42
    int 0x80
user_prog_preempt_a_end:
user_prog_preempt_a_size: dd (user_prog_preempt_a_end - user_prog_preempt_a)

global user_prog_preempt_b
global user_prog_preempt_b_size
user_prog_preempt_b:
    mov ecx, 5000000
.spin:
    dec ecx
    jnz .spin

    mov eax, 1
    mov ebx, 42
    int 0x80
user_prog_preempt_b_end:
user_prog_preempt_b_size: dd (user_prog_preempt_b_end - user_prog_preempt_b)
