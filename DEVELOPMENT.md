# KernelX Development Ledger

---

## 1. Project Overview & Environment

- **Target Architecture**: IA-32 (x86 32-bit Protected Mode)
- **Boot Protocol**: Multiboot Specification v1 (`0x1BADB002`)
- **Compilation Toolchain**:
  - GCC: 15.2.0 (32-bit freestanding target via `-m32 -std=gnu99 -ffreestanding -nostdlib`)
  - Assembler: NASM 2.16.03 (ELF32 output format)
  - Linker: GNU `ld` 2.44 (`-m elf_i386`); kernel includes a small freestanding `__udivdi3` helper
  - ISO Generator: `grub-mkrescue` with `xorriso` 1.5.6
  - Emulation & Debugging: QEMU `qemu-system-i386` 10.2.1, GDB 17.1
- **Primary Host Platform**: Windows 11 with WSL2 Ubuntu (`/mnt/c/Users/cheth/OneDrive/Desktop/kernelX`)

---

## 2. Subsystem Status & Feature Matrix

| Subsystem | Scope / Functionality | Status | Verified In QEMU |
| :--- | :--- | :---: | :---: |
| **Bootstrap & Entry** | Multiboot header, 16KB `.bss` stack, register hand-off, C calling convention | **Completed** | Yes |
| **Linker Script** | 1MB load address (`0x00100000`), 4KB page aligned sections | **Completed** | Yes |
| **VGA Text Driver** | 80x25 memory-mapped I/O (`0xB8000`), scrolling, backspace, tab, cursor control | **Completed** | Yes |
| **Serial UART Driver** | 16550 UART COM1 (`0x3F8`), 38400 baud, loopback self-test, host terminal mirror | **Completed** | Yes |
| **Formatted Printk** | Self-contained formatter (`%s`, `%c`, `%d`, `%u`, `%x`, `%p`), Dual-sink dispatch | **Completed** | Yes |
| **Kernel Panic** | Screen color flip, panic message banner, CPU `cli; hlt` freeze | **Completed** | Yes |
| **GDT & TSS** | Kernel / User code & data descriptors, Task State Segment loaded via LTR | **Completed** | Yes |
| **IDT & Exceptions** | 256 gates, ISR stubs 0-31 with error code handling, CPU register state dumps | **Completed** | Yes |
| **8259 PIC** | Master/Slave remapped (0x20/0x28), cascade line IRQ2, dynamic masking, EOI logic | **Completed** | Yes |
| **8254 PIT** | Channel 0 square-wave mode 3 at 100 Hz, global tick counter, sleep/uptime API | **Completed** | Yes |
| **PS/2 Keyboard** | IRQ1 interrupt-driven scancode set 1 decoder, circular buffer (256B), modifiers | **Completed** | Yes |
| **Physical Memory (PMM)**| Bitmap frame allocator, Multiboot memory map parsing, 4KB page frame management | **Completed** | Yes |
| **Virtual Memory (VMM)** | Two-level paging (CR3, CR0.PG, CR0.WP), dynamic page table allocation, TLB invalidation | **Completed** | Yes |
| **Page Fault Handler** | Vector 14 ISR, CR2 fault address extraction, Non-Present & Write-Protect traps | **Completed** | Yes |
| **Kernel Heap** | Boundary tag block allocator (`kmalloc`, `kzalloc`, `kfree`), Stress-tested coalescing | **Completed** | Yes |
| **Process & Multitasking**| PCB/TCB structures, kernel thread creation, independent 8KB stacks, lifecycle | **Completed** | Yes |
| **Context Switching** | Low-level x86 `switch_to`, callee-saved registers preservation, EFLAGS restoration | **Completed** | Yes |
| **Preemptive Scheduler**| Round-Robin with 50ms time slices, 100 Hz timer-driven preemption, Idle thread (HLT) | **Completed** | Yes |
| **CPU Scheduling Algorithms** | Pluggable framework: Round Robin, Priority (with Aging), MLFQ (4 queues, Boost) | **Completed** | Yes |
| **Scheduler Telemetry & Bench**| Jain's Fairness Index, wait times, automated comparative benchmark suite | **Completed** | Yes |
| **Diagnostic Shell** | Interactive CLI (`KernelX> `), commands: `mem`, `ps`, `sched`, `bench`, `clear`, `help` | **Completed** | Yes |
| **Ring 3 & System Calls** | User mode transition via `iretd`, TSS stack reload, `int 0x80` dispatcher | **Completed** | Yes |
| **VFS & Ramdisk** | VFS path resolution, writable RAMFS, read-only USTAR initrd, per-thread FDs, filesystem syscalls | **Implemented; QEMU verification pending** | Not run in this environment |
| **Interactive Filesystem Shell**| Kernel-resident CLI with `ls`, `cd`, `pwd`, `cat`, `touch`, `mkdir`, `rm`, `write`, `stat`, `tree`, `echo` | **Implemented; QEMU verification pending** | Not run in this environment |

---

## 3. Phase 4 Architecture & Implementation Details

### 3.1 Process & Thread Control Blocks (PCB / TCB)
- **Source Files**: [`include/sched/process.h`](file:///c:/Users/cheth/OneDrive/Desktop/kernelX/include/sched/process.h), [`sched/process.c`](file:///c:/Users/cheth/OneDrive/Desktop/kernelX/sched/process.c).
- **Thread Lifecycle States**: `THREAD_EMBRYO`, `THREAD_READY`, `THREAD_RUNNING`, `THREAD_BLOCKED`, `THREAD_TERMINATED`.
- **Kernel Stack Setup**:
  - 8192-byte (8 KB) stack from the kernel heap.
  - Initial stack context: `arg`, `thread_exit`, `entry` (popped into `EIP`), initial `EFLAGS` (`0x00000202`), and zeroed callee-saved registers (`EBP`, `EBX`, `ESI`, `EDI`).

### 3.2 Low-Level Assembly Context Switching
- **Source File**: [`sched/switch.asm`](file:///c:/Users/cheth/OneDrive/Desktop/kernelX/sched/switch.asm).
- **Signature**: `void switch_to(context_t** prev, context_t* next);`
- Pushes `EFLAGS` (`pushfd`), pushes `ebp`, `ebx`, `esi`, `edi`, swaps `ESP`, restores registers, restores `EFLAGS` (`popfd`), and executes `ret`.
- Updates Task State Segment (`tss.esp0`) to point to the active thread's kernel stack top.

---

## 4. Phase 5 Architecture: Advanced CPU Scheduling Framework

### 4.1 Pluggable Scheduler Interface (`sched_ops_t`)
- **Source Files**: [`include/sched/sched.h`](file:///c:/Users/cheth/OneDrive/Desktop/kernelX/include/sched/sched.h), [`sched/sched.c`](file:///c:/Users/cheth/OneDrive/Desktop/kernelX/sched/sched.c).
- Standard operations table:
  ```c
  typedef struct sched_ops {
      const char* name;
      void        (*init)(void);
      void        (*enqueue)(thread_t* thread);
      thread_t*   (*dequeue)(void);
      void        (*tick)(thread_t* current);
      void        (*yield)(thread_t* current);
      thread_t*   (*drain_all)(void);
  } sched_ops_t;
  ```
- **Zero-Loss Runtime Policy Migration**: When `scheduler_set_policy()` is invoked, `drain_all()` extracts all queued threads from the old scheduler without losing state, re-initializing the queue under the newly selected scheduling policy.

### 4.2 Scheduling Algorithms Implemented
1. **Round Robin (`sched/sched_rr.c`)**:
   - Intrusive FIFO queue via `thread->sched_next` (zero heap allocations during context switches).
   - Configurable time quantum (`DEFAULT_TIME_SLICE = 5 ticks = 50 ms`).
   - Preemption on time slice exhaustion.
2. **Preemptive Priority Scheduling with Aging (`sched/sched_priority.c`)**:
   - 32 discrete priority queues (Level 0 = lowest, Level 31 = highest).
   - $O(1)$ highest-priority queue selection via 32-bit active queue bitmask and CPU instruction `31 - __builtin_clz(active_mask)`.
   - **Immediate Preemption**: Enqueuing a thread with higher priority than the currently executing thread immediately triggers `scheduler_preempt()`.
   - **Dynamic Aging (Starvation Prevention)**: Every 5 timer ticks (`AGING_INTERVAL_TICKS = 5`), all waiting threads have their `dynamic_priority` incremented by 1 and are migrated into the corresponding higher queue, guaranteeing that low-priority threads eventually execute.
3. **Multilevel Feedback Queue (MLFQ) (`sched/sched_mlfq.c`)**:
   - 4 distinct priority queues with exponentially increasing time quantum:
     - **Queue 0**: 2 ticks ($20\,\text{ms}$)
     - **Queue 1**: 4 ticks ($40\,\text{ms}$)
     - **Queue 2**: 8 ticks ($80\,\text{ms}$)
     - **Queue 3**: 16 ticks ($160\,\text{ms}$)
   - **CPU-bound Demotion**: Threads that exhaust their time slice without voluntarily yielding are demoted down one level ($Q_i \to Q_{i+1}$).
   - **Interactive Retention**: Threads that voluntarily yield before their time slice expires retain their current priority queue.
   - **Periodic Starvation Boost**: Every 50 ticks (`MLFQ_BOOST_INTERVAL = 50`), all demoted threads across all queues are reset to `mlfq_level = 0` and placed back into Queue 0.

### 4.3 Scheduler Metrics & Telemetry
- Total context switches, voluntary yields, and timer preemptions.
- **Jain's Fairness Index**:
  $$\mathcal{J}(x_1, x_2, \dots, x_n) = \frac{\left(\sum_{i=1}^n x_i\right)^2}{n \cdot \sum_{i=1}^n x_i^2}$$
  Calculated across all non-idle threads using 64-bit arithmetic via the kernel's `__udivdi3` compiler-runtime helper, scaled to 3 decimal places ($1.000 = \text{perfect fairness}$).

---

## 5. Phase 5 Test Execution & Benchmark Results

The entire automated test suite (`kernel/test_phase5.c`) executed successfully in QEMU:

```
=======================================================================
>>> Beginning Phase 5 Advanced CPU Scheduler Automated Test Suite <<<
=======================================================================

--- TEST 1: Runtime Scheduling Policy Switching ---
  [PASS] Policy switch to Round Robin
  [PASS] Policy switch to Priority Scheduling
  [PASS] Policy switch to MLFQ
  [PASS] Policy switch back to Round Robin

--- TEST 2: Priority Execution Order & Preemption ---
  [PASS] Both priority threads executed to completion
  [PASS] High-priority thread (Prio 30) executed strictly before Low-priority thread (Prio 5)

--- TEST 3: MLFQ Dynamic Demotion & Interactive Retention ---
  [PASS] MLFQ test workloads executed
  [PASS] CPU-bound thread was demoted to lower-priority queue (Q2/Q3)
  [PASS] Yielding interactive thread remained in top-priority queue (Q0)

--- TEST 4: MLFQ Periodic Starvation Boost ---
  [PASS] MLFQ boost thread completed observation window
  [PASS] Periodic boost successfully restored demoted thread from Q3 to Q0

--- TEST 5: Starvation Prevention via Priority Aging ---
  [PASS] Starving low-priority thread acquired CPU via dynamic priority aging

--- TEST 6: High-Frequency Context Switching (10,000+ Decisions) ---
  Completed Decisions : 12500
  [PASS] Exceeded 10,000 live context switches without corruption or deadlock

=======================================================================
             KernelX CPU Scheduler Comparative Benchmark               
=======================================================================
Algorithm: Round Robin
  Context Switches : 29
  CPU Worker Loops : 85965954
  I/O Yield Cycles : 4
  Execution Window : 20 ticks
  Jain Fairness Idx: 0.753

Algorithm: Priority (Aging)
  Context Switches : 29
  CPU Worker Loops : 66881754
  I/O Yield Cycles : 4
  Execution Window : 20 ticks
  Jain Fairness Idx: 0.753

Algorithm: MLFQ (4 Queues)
  Context Switches : 857971
  CPU Worker Loops : 6908019
  I/O Yield Cycles : 428977
  Execution Window : 20 ticks
  Jain Fairness Idx: 0.753

=======================================================================
>>> [PHASE 5 PASSED] All 7 Advanced Scheduler tests & benchmarks passed. <<<
>>> [PHASE 5 COMPLETE] Advanced CPU Scheduling Subsystem verified.
```

### Interactive Shell Commands Verified in QEMU:
- `help`: Command reference listing all available system and scheduler commands.
- `ps`: Full thread telemetry table displaying `TID`, `NAME`, `STATE`, `PRIO`, `DYN`, `MLFQ`, `TICKS`, `WAIT`, and `SWITCHES`.
- `sched`: Telemetry report displaying active policy, context switch counts, and Jain's Fairness Index.
- `sched priority`: Runtime switch to Preemptive Priority Scheduling with Aging.
- `sched mlfq`: Runtime switch to Multilevel Feedback Queue Scheduling.
- `sched rr`: Runtime switch back to Round Robin.
- `bench`: Re-runs comparative benchmark suite across algorithms.

---

## 6. Phase 6 Architecture: User Mode (Ring 3), Privilege Separation & System Calls

### 6.1 Ring 3 Privilege Transition
- **GDT Configuration** ([`arch/x86/gdt.c`](file:///c:/Users/cheth/OneDrive/Desktop/kernelX/arch/x86/gdt.c)):
  - User Code Segment: Selector `0x1B` (Index 3, RPL=3, DPL=3, Present, Executable, Readable).
  - User Data Segment: Selector `0x23` (Index 4, RPL=3, DPL=3, Present, Writable).
- **Task State Segment (TSS)**:
  - Selector `0x28` loaded via `ltr`.
  - On every context switch, `tss.esp0` is reloaded with the active thread's kernel stack top ([`sched/sched.c`](file:///c:/Users/cheth/OneDrive/Desktop/kernelX/sched/sched.c)). When a hardware interrupt or `int 0x80` occurs in Ring 3, the CPU automatically switches to `tss.esp0` before pushing the user privilege context.
- **Hardware Mode Switch** ([`arch/x86/interrupt.asm`](file:///c:/Users/cheth/OneDrive/Desktop/kernelX/arch/x86/interrupt.asm)):
  - Function `enter_user_mode(user_eip, user_esp)` builds the hardware `iretd` frame:
    ```nasm
    push dword 0x23         ; User SS (RPL=3)
    push dword [ebp + 12]   ; User ESP
    push dword 0x00000202   ; User EFLAGS (IF=1)
    push dword 0x1B         ; User CS (RPL=3)
    push dword [ebp + 8]    ; User EIP
    iretd
    ```

### 6.2 Process Address Space Isolation & Paging
- **Source Files**: [`include/mm/vmm.h`](file:///c:/Users/cheth/OneDrive/Desktop/kernelX/include/mm/vmm.h), [`mm/vmm.c`](file:///c:/Users/cheth/OneDrive/Desktop/kernelX/mm/vmm.c).
- **Dedicated Page Directories**: Each user process allocates a distinct physical page directory (`vmm_create_address_space()`).
- **Kernel Mapping Cloning**: Page directory entries `0..11` (<48 MB identity mapping) are copied into each user page directory with `PAGE_USER = 0` (Supervisor Only), ensuring kernel memory is strictly inaccessible from Ring 3.
- **User Region Mapping & Permissions (Audited)**:
  - Code page: `0x40000000` (`PAGE_PRESENT | PAGE_USER`, strictly **Read-Only & Executable**).
  - Data page: `0x40002000` (`PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER`, Read/Write).
  - Stack page: `0x40010000` (Stack top `0x40011000`, 4 KB, Read/Write).
- **PDE Permission Propagation**: `vmm_map_user_page()` sets `PAGE_USER` and `PAGE_WRITABLE` dynamically at the PDE level to ensure matching architectural permissions across both page directory and page table levels.
- **Address Space Switching & Reclamation**: On context switches between processes, `vmm_switch_address_space(next->cr3)` loads CR3 and invalidates TLB. When a process terminates, `vmm_destroy_address_space()` recursively frees all allocated user frames, user page table frames, and the page directory frame.

### 6.3 System Call Interface (`int 0x80`)
- **Source Files**: [`include/arch/x86/syscall.h`](file:///c:/Users/cheth/OneDrive/Desktop/kernelX/include/arch/x86/syscall.h), [`arch/x86/syscall.c`](file:///c:/Users/cheth/OneDrive/Desktop/kernelX/arch/x86/syscall.c), [`arch/x86/interrupt.asm`](file:///c:/Users/cheth/OneDrive/Desktop/kernelX/arch/x86/interrupt.asm).
- **IDT Configuration**: Vector 128 (`0x80`) configured with DPL=3 (`0xEE`), enabling user processes to invoke syscalls via `int 0x80`.
- **Calling Convention (Linux ABI Compatible)**:
  - Syscall Number: `EAX`
  - Arguments: `EBX` (arg1), `ECX` (arg2), `EDX` (arg3), `ESI` (arg4), `EDI` (arg5).
  - Return Value: Stored into `regs->eax` popped by `popa`.
- **Implemented System Calls**:
  - `SYS_EXIT` (1): Terminates user process and triggers immediate scheduler switch.
  - `SYS_YIELD` (2): Voluntarily yields CPU time slice.
  - `SYS_WRITE` (3): Validates user pointer; writes to console or serial (`fd 1` / `fd 2`).
  - `SYS_READ` (4): Validates user buffer; reads input from keyboard/serial.
  - `SYS_GETPID` (5): Returns current process ID (`pid`).
  - `SYS_SLEEP` (6): Sleeps for specified milliseconds using PIT tick counter.
- **Deep Security Pointer Validation (`vmm_validate_user_ptr`)**:
  - Checks for integer overflow in `(uint64_t)start + size`.
  - Rejects null pointers and any address below `USER_SPACE_START` (`0x40000000`) or above `USER_SPACE_END` (`0xC0000000`).
  - Iterates page-by-page across the entire requested range to ensure every page crossed is present and marked `PAGE_USER`.
  - For write-intent buffers (`SYS_READ`), verifies that both PDE and PTE have `PAGE_WRITABLE` set.
  - Correctly verifies zero-length buffers against valid mapped user memory.
  - Returns `-EFAULT` (-14) on any violation without crashing or panicking the kernel.

### 6.4 Security Boundaries & Fault Isolation
1. **Privileged Instruction Isolation**:
   - Ring 3 execution of privileged instructions (e.g., `cli`, `sti`, `in`, `out`, `lgdt`, `ltr`, `mov crX`) triggers CPU General Protection Fault (`#GP`, Vector 13).
   - In [`arch/x86/isr.c`](file:///c:/Users/cheth/OneDrive/Desktop/kernelX/arch/x86/isr.c), if `(regs->cs & 3) == 3`, the offending Ring 3 process is safely terminated via `thread_exit()` rather than halting the system.
2. **Kernel Memory Protection**:
   - Access to supervisor addresses (< 48 MB) triggers Page Fault (`#PF`, Vector 14) with `err_code & 4` (User mode flag) and `err_code & 1` (Protection violation).
   - The fault handler terminates the offending user process and keeps the kernel running.
3. **User Code Page Protection**:
   - Writes to read-only user code pages trigger Page Fault (`#PF`, Vector 14) with `err_code & 2` (Write flag) and `err_code & 1` (Protection violation).
4. **Address Space Isolation**:
   - Multiple user processes writing to the identical virtual address (`0x40002000`) write to separate physical frames without crosstalk or interference.
5. **Preemptive Multitasking Across Ring 3 Processes**:
   - PIT timer interrupts preempt compute-bound Ring 3 processes, safely reloading TSS `esp0` and switching page directories (CR3).

---

## 7. Phase 6 Deep Audit: Bugs Identified & Resolved

During the rigorous security and correctness audit of Phase 6, three defects were uncovered and resolved:

1. **Segment Register Push Bug (`DS = 0x2020023` in Exception Dumps)**:
   - *Discovery*: In previous exception dumps, `DS` was displayed as `0x2020023` instead of the expected selector `0x23`.
   - *Root Cause*: In `arch/x86/interrupt.asm` (`isr_common_stub` and `irq_common_stub`), `mov ax, ds; push eax` wrote only to the lower 16 bits of `eax`, pushing stale bits from prior register operations onto the stack.
   - *Fix*: Changed to `xor eax, eax; mov ax, ds; push eax`, ensuring all 32 bits are cleanly zero-extended. Additionally, `enter_user_mode` now zeroes general-purpose registers (`eax, ebx, ecx, edx, esi, edi, ebp`) before `iretd` to eliminate kernel register state leaks into user space.
2. **User Code Page Write-Protection Deficiency**:
   - *Discovery*: `process_create_user()` initially mapped user code pages with `PAGE_WRITABLE`.
   - *Fix*: Mapped user code pages strictly with `PAGE_PRESENT | PAGE_USER` (read-only and executable). Write attempts to code pages now reliably trigger a write-protection Page Fault (`#PF`), which terminates the faulting process.
3. **Syscall Pointer Validation Edge Cases**:
   - *Discovery*: Pointer validation did not check write permissions on the PDE level, did not handle zero-length buffers, and allowed write-intent buffers to target read-only code pages.
   - *Fix*: Rewrote `vmm_validate_user_ptr()` to validate integer overflow, check PDE and PTE write bits for write intent, iterate across every page crossed by a buffer, and cleanly handle zero-length buffers.

---

## 8. Phase 6 Automated Audit Test Suite & QEMU Verification

The full 8-test deep security and correctness audit passed cleanly in QEMU:

```
=======================================================================
>>> Beginning Phase 6 User Mode & System Call Deep Audit Test Suite <<<
=======================================================================

--- TEST 1: Ring 3 Execution & System Call Dispatching ---
  [PASS] User process created with isolated address space
  [User Ring 3] Execution verified! Testing int 0x80 syscalls...
  [PASS] User process executed in Ring 3 and exited via sys_exit
  [PASS] Process verified as genuine Ring 3 user mode task

--- TEST 2: Privileged Instruction Fault Isolation ---
=======================================================
               UNHANDLED CPU EXCEPTION                 
=======================================================
Vector: 13 [General Protection Fault (#GP)] | Error Code: 0x0

Registers Dump:
  EIP:    0x40000000   CS:  0x1b   EFLAGS: 0x10202
  EAX:    0x0   EBX: 0x0   ECX:    0x0   EDX: 0x0
  ESI:    0x0   EDI: 0x0   EBP:    0x0   ESP: 0x20261f0
  DS:     0x23
=======================================================
[Security] Ring 3 Exception 13 [General Protection Fault (#GP)]: Offending process terminated.

  [PASS] User task created to test privileged instruction fault
  [PASS] Privileged instruction 'cli' in Ring 3 triggered #GP and terminated process safely

--- TEST 3: User Memory Permissions & Page Protection ---
  [PASS] Spawned process to read kernel memory
  [PASS] Ring 3 read of kernel supervisor page (0x00100000) intercepted by #PF
  [PASS] Process reading kernel memory terminated safely
  [PASS] Spawned process to write kernel memory
  [PASS] Ring 3 write to kernel supervisor page intercepted by #PF (Write violation)
  [PASS] Process writing kernel memory terminated safely
  [PASS] Spawned process to write to read-only code page
  [PASS] Writing to read-only user code page (0x40000000) intercepted by #PF (Protection violation)
  [PASS] Process violating code page read-only permission terminated safely
  [PASS] Spawned process to access unmapped page
  [PASS] Reading unmapped page (0x50000000) intercepted by #PF (Non-Present)
  [PASS] Process accessing unmapped address terminated safely

--- TEST 4: Syscall Pointer & Memory Range Security Audit ---
  [PASS] Direct check: NULL pointer rejected
  [PASS] Direct check: Kernel memory pointer (< 0x40000000) rejected
  [PASS] Direct check: Higher-half memory pointer (>= 0xC0000000) rejected
  [PASS] Direct check: Arithmetic overflow (0xFFFFFFF0 + 32) rejected
  [PASS] Direct check: Unmapped user-space pointer rejected
  [PASS] Created user task for comprehensive syscall testing
  [PASS] Ring 3 syscall checks (kernel ptr, overflow, cross-unmapped page, read-only write, NULL, zero-len) all returned -EFAULT / success code 42

--- TEST 5: Virtual Address Space Isolation & Crosstalk Prevention ---
  [PASS] Two user processes instantiated
  [PASS] Processes possess distinct physical page directories (isolated CR3)
  [PASS] Both processes verified private memory at same virtual address without crosstalk

--- TEST 6: Preemptive Multitasking of Ring 3 Processes ---
  [PASS] Created concurrent compute-bound user tasks
  [PASS] Preemptive PIT timer successfully interleaved execution of Ring 3 processes
  [PASS] Context switches occurred across user address spaces during computation

--- TEST 7: Process Lifecycle Stress & Memory Reclamation ---
  [PASS] Created transient user process
  [PASS] Transient process terminated (8 cycles repeated)
  Frames before: 32442, Frames after: 32442
  [PASS] All physical frames (page directory, user code, data, stack) reclaimed without leakage

--- TEST 8: Process Accounting & System Stability Post-Faults ---
  Active Processes: 1, Terminated Processes: 44
  [PASS] At least main/idle process is currently ACTIVE
  [PASS] All terminated fault & test processes accounted in process list
  [PASS] Kernel timer & scheduling remains stable and responsive after all user-mode faults

>>> [PHASE 6 AUDIT PASSED] All 8 User Mode Security & Correctness tests passed! <<<

>>> [PHASE 6 COMPLETE] Ring 3 User Mode, Address Spaces & Syscalls verified.
>>> Type 'help' for commands, 'ps' for task list, 'uproc' to launch Ring 3 task.

KernelX>
```

### Interactive Verification:
- **`uproc`**: Spawns an interactive Ring 3 process with isolated CR3 (`0x124000`), executing `int 0x80` syscalls (`sys_write`, `sys_getpid`, `sys_exit`).
- **`ps`**: Displays execution mode (`Ring 0` vs `Ring 3`) and `PID` alongside `TID`, verifying privilege separation.

---

## 9. Next Steps: Phase 7 Scope (Virtual File System & Ramdisk)

With user mode privilege isolation and system calls verified, the operating system is ready for **Phase 7: Virtual File System (VFS) and Ramdisk**:
1. **VFS Abstraction Layer**:
   - Inode, file descriptor table, and directory operations interface (`open`, `read`, `write`, `close`, `readdir`).
2. **In-Memory Filesystem (Ramdisk / Initrd)**:
   - Load initial ramdisk via Multiboot module.
   - Implement read/write in-memory file storage.
3. **Integration with Syscalls**:
   - Connect `sys_open`, `sys_read`, `sys_write`, `sys_close` to VFS file descriptors.
4. **Shell File Commands**:
   - Implement `ls`, `cat`, `touch`, `mkdir` in KernelX diagnostic shell.


---

## 7. Phase 7: Virtual Filesystem, RAMFS, and Initial Ramdisk

### 7.1 VFS and mount model
- `include/fs/vfs.h`, `fs/vfs.c` define a filesystem-neutral node and operation table.
- Path normalization handles absolute and relative paths, `.`, `..`, repeated separators, and rejects overlong components/path depth rather than silently truncating.
- Mount points redirect path traversal to the mounted filesystem root. The mounted root's parent is connected to the directory containing the mount point.
- Nodes track active opens. RAMFS refuses to unlink an open node or a mounted filesystem root.

### 7.2 Writable RAMFS
- `fs/ramfs.c` provides in-memory files and directories, linked directory entries, file read/write, metadata, creation, `mkdir`, and unlink.
- File writes grow storage in 256-byte units. Allocation arithmetic is overflow-checked.
- Newly allocated storage and sparse-write gaps are zero-filled so reads never expose uninitialized heap contents.
- `vfs_truncate()` releases a RAMFS file's data allocation and resets its length.
- Empty directories may be removed; non-empty directories are rejected.

### 7.3 Read-only initial ramdisk
- GRUB loads `/boot/initrd.tar` as a Multiboot module.
- `mm/pmm.c` reserves the Multiboot module descriptor and module payload so the physical allocator cannot reuse archive memory.
- `fs/initrd.c` parses bounded 512-byte USTAR records, validates octal sizes, checks header checksums, verifies payload and padding bounds, honors the USTAR prefix field, and creates nested directories.
- `kernel/udivdi3.c` supplies the freestanding 64-bit unsigned division helper required by scheduler fairness metrics, so the kernel link does not depend on a host multilib `libgcc.a`.
- The archive is mounted read-only at `/initrd`. If a module is present but malformed, initialization fails instead of silently hiding the malformed archive behind fallback files.
- The fallback archive is used only when the bootloader supplies no module.

### 7.4 File descriptors and syscalls
- Each thread owns an independent descriptor table. Descriptors 0, 1, and 2 are reserved for standard input, output, and error.
- Open/close update node open references. Unlink is rejected while a file is open.
- `int 0x80` exposes `open`, `close`, `read`, `write`, `mkdir`, `unlink`, `stat`, and `readdir` in addition to the earlier process and timing calls.
- User path strings are copied into a bounded kernel buffer one byte at a time with user-page validation. Stat and directory-entry output buffers are validated before kernel writes.
- Read/write syscalls enforce descriptor access mode.

### 7.5 Phase 7 regression coverage
`kernel/test_phase7.c` exercises root initialization, nested directories, path normalization, file create/read/write/stat/unlink, descriptor allocation, initrd reads and read-only behavior, sparse writes, integer-overflow rejection, open-file unlink protection, truncate, and nested initrd paths.

**Verification status:** all Phase 7 C translation units compile with the freestanding 32-bit GCC flags. A separate host-side RAMFS/VFS harness passed path normalization, sparse-file zero filling, overflow rejection, open-reference unlink protection, and truncate checks. QEMU/GRUB runtime execution has not been performed in the current build environment.

---

## 8. Phase 8: Interactive Filesystem Shell

The shell remains kernel-resident (it is not yet an independently loaded Ring 3 shell executable). It reads keyboard input and the COM1 serial console and executes filesystem operations through the VFS.

| Command | Behavior |
|---|---|
| `help` | Show command reference |
| `ls [path]` | List files and directories |
| `pwd` | Print normalized current working directory |
| `cd [path]` | Change directory; supports `.`, `..`, absolute and relative paths |
| `cat <file>` | Read and print file contents |
| `touch <file>` | Create an empty file; existing regular files are left unchanged |
| `mkdir <dir>` | Create a directory |
| `rm <path>` | Remove a file or empty directory |
| `write <file> <text>` | Append text and a newline to a RAMFS file |
| `stat <path>` | Display inode, size, type, and permissions |
| `tree [path]` | Recursively display directory contents (bounded depth) |
| `echo <text>` | Print text |
| `mem`, `ps`, `sched`, `bench`, `uproc`, `clear` | Existing diagnostics and process/scheduler commands |

The prompt displays the current working directory. Input is accepted from the PS/2 keyboard or serial port; command input is bounded by the fixed shell buffer.

**Verification status:** the shell C source compiles with the freestanding 32-bit flags. Interactive QEMU keyboard/serial testing remains pending.

---

## 9. Build and Verification Notes

The current source snapshot was checked in the following ways:
- Compiled every C source file under `arch/`, `drivers/`, `fs/`, `kernel/`, `mm/`, and `sched/` using:
  `gcc -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude -nostdlib -fno-builtin -fno-stack-protector -fno-pie`.
- Ran a host-side VFS/RAMFS harness: **PASS**.
- The C code and final ELF link were checked in the editing environment using the existing unchanged assembly object files. Full NASM regeneration, GRUB ISO generation, and QEMU boot/regression execution were not available because `nasm`, `grub-mkrescue`, `xorriso`, and `qemu-system-i386` were not installed.

On the intended Ubuntu/WSL2 development machine, install missing tools with:
`sudo apt-get update && sudo apt-get install -y build-essential gcc-multilib nasm qemu-system-x86 gdb xorriso grub-pc-bin grub-common`

Then run:
1. `make clean`
2. `make`
3. `make iso`
4. `make run-iso`

Do not mark Phase 7 or Phase 8 as QEMU-verified until the new ISO has booted and the Phase 7 test output plus interactive shell commands have been observed.

### Known limitations
- The filesystem is in-memory; there is no persistent disk block driver or on-disk filesystem.
- The initrd parser targets classic USTAR records and does not implement PAX extended headers, GNU long-name records, symlinks, or hard links.
- The current shell is a kernel-resident command loop, not a Ring 3 executable loaded by an ELF program loader.
- The current 32-bit non-PAE paging configuration has no NX page permission; read-only user code pages do not imply non-executable data pages.
