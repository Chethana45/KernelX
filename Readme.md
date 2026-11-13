# ⚙️ KernelX — Building an Operating System from Scratch

<p align="center">
  <strong>A Modular 32-bit x86 Operating System Built from Scratch</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Language-C%20%7C%20Assembly-blue?style=for-the-badge" />
  <img src="https://img.shields.io/badge/Architecture-x86%2032--bit-orange?style=for-the-badge" />
  <img src="https://img.shields.io/badge/Bootloader-GRUB-red?style=for-the-badge" />
  <img src="https://img.shields.io/badge/Emulator-QEMU-green?style=for-the-badge" />
</p>

## 🚀 Overview

**KernelX** is a modular, 32-bit x86 operating system developed from scratch using C and Assembly.

The project explores the internal architecture of modern operating systems by implementing fundamental kernel subsystems, including bootstrapping, interrupt handling, physical and virtual memory management, kernel heap allocation, process management, preemptive multitasking, CPU scheduling, hardware privilege separation, system calls, and filesystem management.

Rather than relying on an existing operating system kernel, KernelX builds its core functionality from the ground up, providing hands-on experience with low-level programming, processor architecture, memory protection, and operating system design.

The ultimate goal is to develop a functional, modular operating system with isolated user processes, filesystem support, and an interactive command-line environment.

## ✨ Features

### 1. Bootloader & Kernel Initialization
- GRUB-based boot process using the Multiboot specification.
- Custom kernel entry point written in Assembly.
- Kernel stack initialization.
- VGA text-mode console.
- Serial communication through COM1.
- Modular kernel initialization.

### 2. Interrupt & Hardware Management
- Global Descriptor Table (GDT).
- Interrupt Descriptor Table (IDT).
- CPU exception handling.
- Interrupt Service Routines (ISRs).
- Programmable Interrupt Controller (8259 PIC).
- Programmable Interval Timer (8254 PIT).
- PS/2 keyboard driver.
- Hardware interrupt handling and End-of-Interrupt management.

### 3. Memory Management
- Physical Memory Manager (PMM).
- Bitmap-based physical frame allocation.
- Virtual Memory Manager (VMM).
- x86 two-level paging architecture.
- 4 KB page mapping and unmapping.
- Page fault handling.
- Kernel heap allocator.
- Dynamic memory allocation and deallocation.
- Memory protection using page permissions.

### 4. Process & Thread Management
- Process Control Blocks (PCB).
- Thread Control Blocks (TCB).
- Independent kernel stacks.
- Context switching using Assembly.
- Process lifecycle management.
- Thread creation and termination.
- Ready queues and scheduling.
- Idle thread management.

### 5. Advanced CPU Scheduling

KernelX implements multiple CPU scheduling algorithms through a modular scheduler interface.

| Algorithm | Description |
|---|---|
| Round Robin | Time-sliced CPU scheduling |
| Priority Scheduling | Priority-based task selection |
| MLFQ | Multi-Level Feedback Queue scheduling |

Additional capabilities:
- Dynamic priority adjustment.
- Priority aging.
- Starvation prevention.
- MLFQ queue demotion and priority boosting.
- Runtime scheduling policy switching.
- Scheduling statistics and fairness measurements.

### 6. User Mode & System Calls

KernelX implements x86 hardware privilege separation.

- Ring 0 kernel execution.
- Ring 3 user-mode execution.
- Per-process virtual address spaces.
- Separate page directories.
- TSS-based kernel stack switching.
- User/kernel memory protection.
- `int 0x80` system call interface.
- User pointer validation.
- Process fault isolation.

Implemented system calls:

| System Call | Function |
|---|---|
| `sys_write` | Output data |
| `sys_read` | Read input |
| `sys_getpid` | Retrieve process ID |
| `sys_yield` | Voluntarily yield CPU |
| `sys_exit` | Terminate process |
| `sys_sleep` | Sleep for a specified duration |

### 7. Virtual File System & Ramdisk

KernelX includes a modular filesystem subsystem.

- Virtual File System (VFS).
- Writable RAM filesystem (RAMFS).
- Initial RAM disk (initrd).
- TAR archive parsing.
- Nested directory support.
- File creation and deletion.
- File reading and writing.
- File metadata and size management.
- File descriptor management.
- Filesystem path resolution.
- Read-only initial ramdisk protection.
- Memory reclamation after file deletion.

### 8. Interactive Kernel Shell

KernelX provides an interactive command-line shell for filesystem and kernel operations.

| Command | Function |
|---|---|
| `help` | Display available commands |
| `ls` | List directory contents |
| `pwd` | Display current directory |
| `cd` | Change directory |
| `cat` | Display file contents |
| `touch` | Create a file |
| `mkdir` | Create a directory |
| `rm` | Remove a file |
| `write` | Write content to a file |
| `stat` | Display file metadata |
| `tree` | Display directory hierarchy |
| `mem` | Display memory information |
| `ps` | Display process information |
| `sched` | Inspect scheduling information |
| `uproc` | Launch a user-mode process |

## 🏗️ Architecture

KernelX follows a modular kernel architecture.

```text
                 KERNELX OPERATING SYSTEM
                 =======================

                    GRUB BOOTLOADER
                          |
                          v
                   KERNEL ENTRY POINT
                          |
                          v
                  KERNEL INITIALIZATION
                          |
          +---------------+---------------+
          |               |               |
          v               v               v
     INTERRUPTS        MEMORY         DRIVERS
     & EXCEPTIONS     MANAGEMENT      VGA / SERIAL
          |               |           KEYBOARD
          |               |
          +-------+-------+
                  |
                  v
           PROCESS MANAGEMENT
                  |
                  v
           CPU SCHEDULER
          /      |       \
         v       v        v
        RR    PRIORITY    MLFQ
                  |
                  v
          USER MODE / RING 3
                  |
                  v
            SYSTEM CALLS
                  |
                  v
           VIRTUAL FILE SYSTEM
                  |
           +------+------+
           |             |
           v             v
          RAMFS         INITRD
           |             |
           v             v
      WRITABLE RAM    READ-ONLY TAR
      FILESYSTEM       ARCHIVE
```

## 🛠️ Technology Stack

| Technology | Purpose |
|---|---|
| C | Kernel and subsystem implementation |
| x86 Assembly | Bootstrapping, interrupt handling and context switching |
| NASM | Assembly language development |
| GCC | 32-bit kernel compilation |
| GNU LD | Kernel linking |
| GRUB | Bootloader |
| Multiboot | Kernel boot protocol |
| QEMU | Operating system emulation |
| GDB | Kernel debugging |
| Make | Build automation |
| xorriso | Bootable ISO generation |

## 📂 Project Structure

```text
KernelX/
│
├── arch/
│   └── x86/
│       ├── gdt.c
│       ├── idt.c
│       ├── interrupt.asm
│       ├── isr.c
│       ├── pic.c
│       ├── pit.c
│       └── syscall.c
│
├── boot/
│   ├── boot.asm
│   └── grub.cfg
│
├── drivers/
│   ├── keyboard.c
│   ├── serial.c
│   └── vga.c
│
├── fs/
│   ├── vfs.c
│   ├── ramfs.c
│   └── initrd.c
│
├── include/
│   ├── arch/
│   ├── drivers/
│   ├── fs/
│   ├── kernel/
│   ├── mm/
│   └── sched/
│
├── kernel/
│   ├── kmain.c
│   ├── printk.c
│   └── test_phase*.c
│
├── mm/
│   ├── pmm.c
│   ├── vmm.c
│   ├── kheap.c
│   └── page_fault.c
│
├── sched/
│   ├── process.c
│   ├── sched.c
│   ├── sched_rr.c
│   ├── sched_priority.c
│   └── sched_mlfq.c
│
├── Makefile
├── linker.ld
└── DEVELOPMENT.md
```

## 💻 Getting Started

### Prerequisites

KernelX is developed and tested in a Linux environment using Ubuntu on WSL2.

Install the required dependencies:

```bash
sudo apt update

sudo apt install -y \
    build-essential \
    gcc-multilib \
    nasm \
    qemu-system-x86 \
    gdb \
    xorriso \
    grub-pc-bin \
    grub-common \
    make
```

### Clone the Repository

```bash
git clone https://github.com/Chethana45/KernelX.git

cd KernelX
```

### Build the Kernel

```bash
make clean
make
```

### Generate Bootable ISO

```bash
make iso
```

### Run KernelX in QEMU

```bash
make run-iso
```

QEMU launches a virtual machine and boots KernelX through GRUB.

## 🧪 Testing & Verification

KernelX includes phase-specific automated test suites.

| Phase | Testing Focus |
|---|---|
| Phase 1 | Boot process and console |
| Phase 2 | GDT, IDT, interrupts and hardware |
| Phase 3 | Physical memory, paging and heap |
| Phase 4 | Process management and multitasking |
| Phase 5 | Scheduling algorithms |
| Phase 6 | Ring 3, system calls and isolation |
| Phase 7 | VFS, RAMFS and initial ramdisk |
| Phase 8 | Interactive shell functionality |

The Phase 7 QEMU test report records 16 passing filesystem tests covering VFS operations, RAMFS behavior, initial ramdisk access, invalid filesystem operations, and memory reclamation.

## 🎯 Learning Objectives

This project explores:

- Operating system architecture and kernel design.
- Low-level C and Assembly programming.
- x86 processor architecture.
- Interrupts, exceptions and hardware interaction.
- Physical and virtual memory management.
- Process scheduling and context switching.
- CPU privilege levels and memory protection.
- System call implementation.
- Filesystem architecture and storage abstractions.
- Kernel debugging and QEMU-based testing.

## 🚧 Future Improvements

Potential directions for further development:

- ELF executable loading.
- Independent Ring 3 shell.
- User-space applications.
- Keyboard and terminal improvements.
- Persistent filesystem support.
- ATA disk driver.
- Disk-based filesystem.
- Inter-process communication (IPC).
- Multiterminal support.
- Networking stack.
- SMP and multi-core scheduling.

## 👩‍💻 Author

**Chethana Sri**

Computer Science Engineering Student  
Operating Systems | Systems Programming | Full-Stack Development

- GitHub: [@Chethana45](https://github.com/Chethana45)

## ⭐ Support

If you find this project interesting, consider giving the repository a ⭐ on GitHub!

---

<p align="center">
  <strong>KernelX — Understanding computers by building the system that runs them.</strong>
</p>
