# KernelX Makefile
# Targets 32-bit x86 Protected Mode Bare-Metal Environment

CC      := gcc
AS      := nasm
LD      := ld
QEMU    := qemu-system-i386

CFLAGS  := -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude -nostdlib -fno-builtin -fno-stack-protector -fno-pie
ASFLAGS := -f elf32
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib

BUILD_DIR := build
ISO_DIR   := $(BUILD_DIR)/isodir

OBJS := \
	$(BUILD_DIR)/boot.o \
	$(BUILD_DIR)/gdt.o \
	$(BUILD_DIR)/gdt_flush.o \
	$(BUILD_DIR)/idt.o \
	$(BUILD_DIR)/idt_flush.o \
	$(BUILD_DIR)/isr.o \
	$(BUILD_DIR)/interrupt.o \
	$(BUILD_DIR)/user_tests.o \
	$(BUILD_DIR)/switch.o \
	$(BUILD_DIR)/pic.o \
	$(BUILD_DIR)/pit.o \
	$(BUILD_DIR)/syscall.o \
	$(BUILD_DIR)/vga.o \
	$(BUILD_DIR)/serial.o \
	$(BUILD_DIR)/keyboard.o \
	$(BUILD_DIR)/pmm.o \
	$(BUILD_DIR)/vmm.o \
	$(BUILD_DIR)/page_fault.o \
	$(BUILD_DIR)/kheap.o \
	$(BUILD_DIR)/process.o \
	$(BUILD_DIR)/sched.o \
	$(BUILD_DIR)/sched_rr.o \
	$(BUILD_DIR)/sched_priority.o \
	$(BUILD_DIR)/sched_mlfq.o \
	$(BUILD_DIR)/printk.o \
	$(BUILD_DIR)/udivdi3.o \
	$(BUILD_DIR)/vfs.o \
	$(BUILD_DIR)/ramfs.o \
	$(BUILD_DIR)/initrd.o \
	$(BUILD_DIR)/test_phase2.o \
	$(BUILD_DIR)/test_phase3.o \
	$(BUILD_DIR)/test_phase4.o \
	$(BUILD_DIR)/test_phase5.o \
	$(BUILD_DIR)/test_phase6.o \
	$(BUILD_DIR)/test_phase7.o \
	$(BUILD_DIR)/kmain.o

KERNEL_ELF := $(BUILD_DIR)/kernelx.elf
KERNEL_ISO := $(BUILD_DIR)/kernelx.iso
INITRD_TAR := $(BUILD_DIR)/initrd.tar

.PHONY: all clean run run-iso debug verify iso

all: $(KERNEL_ELF)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Assembly objects
$(BUILD_DIR)/boot.o: boot/boot.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/gdt_flush.o: arch/x86/gdt_flush.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/idt_flush.o: arch/x86/idt_flush.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/interrupt.o: arch/x86/interrupt.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/user_tests.o: arch/x86/user_tests.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/switch.o: sched/switch.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

# C objects: arch & drivers
$(BUILD_DIR)/gdt.o: arch/x86/gdt.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/idt.o: arch/x86/idt.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/isr.o: arch/x86/isr.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pic.o: arch/x86/pic.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pit.o: arch/x86/pit.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/syscall.o: arch/x86/syscall.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vga.o: drivers/vga.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/serial.o: drivers/serial.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/keyboard.o: drivers/keyboard.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# C objects: memory management
$(BUILD_DIR)/pmm.o: mm/pmm.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vmm.o: mm/vmm.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/page_fault.o: mm/page_fault.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kheap.o: mm/kheap.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# C objects: process & scheduler
$(BUILD_DIR)/process.o: sched/process.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sched.o: sched/sched.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sched_rr.o: sched/sched_rr.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sched_priority.o: sched/sched_priority.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/sched_mlfq.o: sched/sched_mlfq.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# C objects: filesystem
$(BUILD_DIR)/vfs.o: fs/vfs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ramfs.o: fs/ramfs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/initrd.o: fs/initrd.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# C objects: kernel & tests
$(BUILD_DIR)/printk.o: kernel/printk.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/udivdi3.o: kernel/udivdi3.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_phase2.o: kernel/test_phase2.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_phase3.o: kernel/test_phase3.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_phase4.o: kernel/test_phase4.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_phase5.o: kernel/test_phase5.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_phase6.o: kernel/test_phase6.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_phase7.o: kernel/test_phase7.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kmain.o: kernel/kmain.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Initial Ramdisk archive creation
$(INITRD_TAR): Makefile | $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/initrd_root
	echo "Welcome to KernelX Educational Operating System!" > $(BUILD_DIR)/initrd_root/welcome.txt
	echo "KernelX Phase 7: Initial ramdisk loaded via boot archive parser." > $(BUILD_DIR)/initrd_root/readme.txt
	echo "version=1.0" > $(BUILD_DIR)/initrd_root/system.cfg
	mkdir -p $(BUILD_DIR)/initrd_root/docs
	echo "Nested TAR path parsing works in KernelX." > $(BUILD_DIR)/initrd_root/docs/phase7.txt
	tar -cf $(INITRD_TAR) -C $(BUILD_DIR)/initrd_root welcome.txt readme.txt system.cfg docs

# Linking
$(KERNEL_ELF): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

# ISO generation
$(KERNEL_ISO): $(KERNEL_ELF) $(INITRD_TAR) boot/grub.cfg
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/kernelx.elf
	cp $(INITRD_TAR) $(ISO_DIR)/boot/initrd.tar
	cp boot/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(KERNEL_ISO) $(ISO_DIR) 2>/dev/null

iso: $(KERNEL_ISO)

# Direct kernel boot in QEMU with serial port attached to stdio
run: $(KERNEL_ELF) $(INITRD_TAR)
	$(QEMU) -kernel $(KERNEL_ELF) -initrd $(INITRD_TAR) -serial stdio

# ISO boot in QEMU simulating real BIOS -> GRUB -> Kernel chain
run-iso: $(KERNEL_ISO)
	$(QEMU) -cdrom $(KERNEL_ISO) -serial stdio

# Debug mode: starts QEMU frozen at entry point on localhost:1234
debug: $(KERNEL_ELF) $(INITRD_TAR)
	$(QEMU) -kernel $(KERNEL_ELF) -initrd $(INITRD_TAR) -s -S -serial stdio

# Non-interactive automated test run
verify: $(KERNEL_ELF) $(INITRD_TAR)
	$(QEMU) -kernel $(KERNEL_ELF) -initrd $(INITRD_TAR) -display none -serial stdio

clean:
	rm -rf $(BUILD_DIR)
