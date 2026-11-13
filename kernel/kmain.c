#include <kernel/types.h>
#include <kernel/multiboot.h>
#include <kernel/printk.h>
#include <drivers/vga.h>
#include <drivers/serial.h>
#include <drivers/keyboard.h>
#include <arch/x86/gdt.h>
#include <arch/x86/idt.h>
#include <arch/x86/isr.h>
#include <arch/x86/pic.h>
#include <arch/x86/pit.h>
#include <arch/x86/syscall.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/page_fault.h>
#include <mm/kheap.h>
#include <sched/process.h>
#include <sched/sched.h>
#include <fs/vfs.h>
#include <fs/ramfs.h>
#include <fs/initrd.h>
#include <kernel/test_phase2.h>
#include <kernel/test_phase3.h>
#include <kernel/test_phase4.h>
#include <kernel/test_phase5.h>
#include <kernel/test_phase6.h>
#include <kernel/test_phase7.h>

extern const uint8_t user_prog_basic[];
extern const uint32_t user_prog_basic_size;

static void display_banner(void) {
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    printk("  _  __                    _ __   __\n");
    printk(" | |/ /___ _ __ _ __   ___| |\\ \\ / /\n");
    printk(" | ' // _ \\ '__| '_ \\ / _ \\ | \\ V / \n");
    printk(" | . \\  __/ |  | | | |  __/ | / . \\ \n");
    printk(" |_|\\_\\___|_|  |_| |_|\\___|_|/_/ \\_\\\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk(" ----------------------------------------------------------------------\n");
    printk(" KernelX Operating System | 32-bit x86 Protected Mode Bare-Metal Kernel\n");
    printk(" ----------------------------------------------------------------------\n\n");
}

static bool str_equals(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 == *(const unsigned char*)s2;
}

static bool str_starts_with(const char* str, const char* prefix) {
    while (*prefix) {
        if (*prefix++ != *str++) return false;
    }
    return true;
}

static const char* skip_spaces(const char* s) {
    while (*s == ' ') s++;
    return s;
}

static void kstrncpy_local(char* dest, const char* src, size_t n) {
    size_t i = 0;
    while (src && src[i] && i < n - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static void print_prompt(void) {
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    thread_t* curr = thread_get_current();
    printk("KernelX:%s> ", (curr && curr->cwd[0] != '\0') ? curr->cwd : "/");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

/* ========================================================================= */
/* Shell Filesystem Commands                                                 */
/* ========================================================================= */
static void shell_cmd_ls(const char* arg) {
    thread_t* curr = thread_get_current();
    const char* path = (arg && *arg) ? arg : (curr ? curr->cwd : "/");
    vfs_node_t* node = vfs_resolve_path(path, curr ? curr->cwd : "/");
    if (!node) {
        printk("ls: cannot access '%s': No such file or directory\n\n", path);
        return;
    }
    if (!(node->type & VFS_TYPE_DIRECTORY)) {
        printk("%s (%u B)\n\n", node->name, node->size);
        return;
    }

    vfs_dirent_t dent;
    uint32_t idx = 0;
    while (vfs_readdir(node, idx++, &dent) == 0) {
        if (dent.type & VFS_TYPE_DIRECTORY) {
            vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            printk("  <DIR>  %s\n", dent.name);
        } else {
            vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            printk("  %5u  %s\n", dent.size, dent.name);
        }
    }
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("\n");
}

static void shell_cmd_cd(const char* arg) {
    thread_t* curr = thread_get_current();
    if (!curr) return;
    const char* path = (arg && *arg) ? arg : "/";
    char normalized[VFS_PATH_MAX];

    if (vfs_normalize_path(normalized, path, curr->cwd) != 0) {
        printk("cd: invalid or overlong path\n\n");
        return;
    }

    vfs_node_t* node = vfs_resolve_path(normalized, "/");
    if (!node) {
        printk("cd: '%s': No such file or directory\n\n", normalized);
        return;
    }
    if (!(node->type & VFS_TYPE_DIRECTORY)) {
        printk("cd: '%s': Not a directory\n\n", normalized);
        return;
    }

    kstrncpy_local(curr->cwd, normalized, VFS_PATH_MAX);
}

static void shell_cmd_pwd(void) {
    thread_t* curr = thread_get_current();
    printk("%s\n\n", (curr && curr->cwd[0]) ? curr->cwd : "/");
}

static void shell_cmd_cat(const char* arg) {
    if (!arg || !*arg) {
        printk("Usage: cat <file>\n\n");
        return;
    }
    thread_t* curr = thread_get_current();
    vfs_node_t* node = vfs_resolve_path(arg, curr ? curr->cwd : "/");
    if (!node) {
        printk("cat: '%s': No such file or directory\n\n", arg);
        return;
    }
    if (node->type & VFS_TYPE_DIRECTORY) {
        printk("cat: '%s': Is a directory\n\n", arg);
        return;
    }

    uint8_t buf[256];
    uint32_t offset = 0;
    while (offset < node->size) {
        int32_t r = vfs_read(node, offset, sizeof(buf) - 1, buf);
        if (r <= 0) break;
        buf[r] = '\0';
        for (int i = 0; i < r; i++) {
            vga_putc(buf[i]);
            if (buf[i] == '\n') serial_putc(COM1_PORT, '\r');
            serial_putc(COM1_PORT, buf[i]);
        }
        offset += r;
    }
    printk("\n\n");
}

static void shell_cmd_touch(const char* arg) {
    if (!arg || !*arg) {
        printk("Usage: touch <file>\n\n");
        return;
    }
    thread_t* curr = thread_get_current();
    const char* cwd = curr ? curr->cwd : "/";
    vfs_node_t* existing = vfs_resolve_path(arg, cwd);
    if (existing) {
        if (existing->type & VFS_TYPE_FILE) return;
        printk("touch: '%s' is not a regular file\n\n", arg);
        return;
    }
    if (vfs_create(arg, cwd, 0644) != 0) {
        printk("touch: cannot create '%s'\n\n", arg);
    }
}

static void shell_cmd_mkdir(const char* arg) {
    if (!arg || !*arg) {
        printk("Usage: mkdir <directory>\n\n");
        return;
    }
    thread_t* curr = thread_get_current();
    int r = vfs_mkdir(arg, curr ? curr->cwd : "/", 0755);
    if (r != 0) {
        printk("mkdir: cannot create directory '%s'\n\n", arg);
    }
}

static void shell_cmd_rm(const char* arg) {
    if (!arg || !*arg) {
        printk("Usage: rm <file_or_dir>\n\n");
        return;
    }
    thread_t* curr = thread_get_current();
    int r = vfs_unlink(arg, curr ? curr->cwd : "/");
    if (r != 0) {
        printk("rm: cannot remove '%s'\n\n", arg);
    }
}

static void shell_cmd_write(const char* arg) {
    if (!arg || !*arg) {
        printk("Usage: write <file> <text>\n\n");
        return;
    }

    char path[VFS_PATH_MAX];
    size_t pi = 0;
    while (*arg && *arg != ' ' && pi < VFS_PATH_MAX - 1) path[pi++] = *arg++;
    path[pi] = '\0';
    if (*arg && *arg != ' ') {
        printk("write: path is too long\n\n");
        return;
    }
    arg = skip_spaces(arg);

    thread_t* curr = thread_get_current();
    const char* cwd = curr ? curr->cwd : "/";
    vfs_node_t* node = vfs_resolve_path(path, cwd);
    if (!node) {
        if (vfs_create(path, cwd, 0644) == 0) node = vfs_resolve_path(path, cwd);
    }
    if (!node) {
        printk("write: cannot open '%s'\n\n", path);
        return;
    }
    if (node->type & VFS_TYPE_DIRECTORY) {
        printk("write: '%s' is a directory\n\n", path);
        return;
    }

    size_t wlen = 0;
    while (arg[wlen]) wlen++;
    int32_t written = vfs_write(node, node->size, wlen, (const uint8_t*)arg);
    if (written < 0) {
        printk("write: failed to write '%s'\n\n", path);
        return;
    }
    int32_t newline_written = vfs_write(node, node->size, 1, (const uint8_t*)"\n");
    printk("Wrote %u bytes to '%s'\n\n",
           (uint32_t)(written + (newline_written > 0 ? newline_written : 0)), path);
}

static void shell_cmd_stat(const char* arg) {
    if (!arg || !*arg) {
        printk("Usage: stat <path>\n\n");
        return;
    }
    thread_t* curr = thread_get_current();
    vfs_stat_t st;
    if (vfs_stat(arg, curr ? curr->cwd : "/", &st) != 0) {
        printk("stat: cannot stat '%s': No such file or directory\n\n", arg);
        return;
    }
    printk("  File : %s\n", arg);
    printk("  Size : %u bytes\n", st.size);
    printk("  Type : %s\n", (st.type & VFS_TYPE_DIRECTORY) ? "Directory" : "Regular File");
    printk("  Inode: %u\n", st.ino);
    printk("  Perm : 0%o\n\n", st.permissions);
}

static void shell_tree_recurse(vfs_node_t* node, int depth) {
    if (!node || depth > 6) return;
    vfs_dirent_t dent;
    uint32_t idx = 0;
    while (vfs_readdir(node, idx++, &dent) == 0) {
        if (str_equals(dent.name, ".") || str_equals(dent.name, "..")) continue;
        for (int i = 0; i < depth; i++) printk("  ");
        if (dent.type & VFS_TYPE_DIRECTORY) {
            vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            printk("|-- %s/\n", dent.name);
            vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            vfs_node_t* child = vfs_finddir(node, dent.name);
            if (child) shell_tree_recurse(child, depth + 1);
        } else {
            vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            printk("|-- %s (%u B)\n", dent.name, dent.size);
        }
    }
}

static void shell_cmd_tree(const char* arg) {
    thread_t* curr = thread_get_current();
    const char* path = (arg && *arg) ? arg : (curr ? curr->cwd : "/");
    vfs_node_t* node = vfs_resolve_path(path, curr ? curr->cwd : "/");
    if (!node) {
        printk("tree: '%s': No such file or directory\n\n", path);
        return;
    }
    printk("%s\n", path);
    shell_tree_recurse(node, 0);
    printk("\n");
}

void kmain(uint32_t magic, multiboot_info_t* mbd) {
    /* 1. Initialize hardware text console & UART serial port */
    vga_init();
    int serial_status = serial_init(COM1_PORT);

    /* 2. Show system startup banner */
    display_banner();

    /* 3. Verify Serial logging channel */
    if (serial_status == 0) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        printk("[OK] ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        printk("Serial COM1 initialized at 38400 baud (host mirror active)\n");
    }

    /* 4. Validate Multiboot specification compliance */
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        panic("Invalid Multiboot magic number! Halting boot.");
    }

    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[OK] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Multiboot hand-off verified (magic: %x, info: %p)\n", magic, mbd);

    /* 5. Initialize GDT & TSS */
    gdt_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[OK] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("GDT & TSS initialized (Kernel & User segments loaded)\n");

    /* 6. Initialize IDT, ISR dispatcher & System Call Interface */
    idt_init();
    isr_init();
    syscall_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[OK] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("IDT & System Calls initialized (int 0x80 registered with DPL=3)\n");

    /* 7. Initialize & remap 8259 PIC */
    pic_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[OK] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("8259 PIC remapped (Master: IRQ 32-39, Slave: IRQ 40-47)\n");

    /* 8. Initialize 8254 PIT timer at 100 Hz */
    pit_init(100);
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[OK] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("8254 PIT configured at 100 Hz (IRQ0 active)\n");

    /* 9. Initialize PS/2 Keyboard Driver */
    keyboard_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[OK] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("PS/2 Keyboard driver initialized (IRQ1 active)\n");

    /* 10. Enable hardware interrupts */
    __asm__ volatile ("sti");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[OK] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Hardware interrupts enabled (STI executed)\n");

    /* 11. Run automated Phase 2 subsystem test suite */
    run_phase2_tests();

    /* 12. Initialize Physical Memory Manager (PMM) */
    pmm_init(mbd);
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[OK] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("PMM initialized (Bitmap allocator, Total Frames: %u)\n", pmm_get_total_frames());

    /* 13. Initialize Virtual Memory Manager (VMM) & Paging */
    vmm_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[OK] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("VMM initialized (Paging enabled, 12 Page Tables mapped)\n");

    /* 14. Initialize Page Fault Handler */
    page_fault_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[OK] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Page Fault handler registered on vector 14\n");

    /* 15. Initialize Kernel Heap (512 KB initial heap at 0x02000000) */
    kheap_init(0x02000000, 512 * 1024);
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[OK] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Kernel Heap initialized (512 KB pool at 0x02000000)\n");

    /* 16. Run automated Phase 3 subsystem test suite */
    run_phase3_tests();

    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk(">>> [PHASE 3 COMPLETE] Physical & Virtual Memory Subsystem verified.\n\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    /* 17. Initialize Multitasking & Scheduler */
    scheduler_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[OK] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Multitasking initialized (PCB/TCB queues, Idle thread TID 0 active)\n");

    scheduler_start();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[OK] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Preemptive CPU Scheduler active (100 Hz timer tick preemption)\n");

    /* 18. Run automated Phase 4 subsystem test suite */
    run_phase4_tests();

    /* 19. Run automated Phase 5 subsystem test suite */
    run_phase5_tests();

    /* 20. Run automated Phase 6 subsystem test suite */
    run_phase6_tests();

    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk(">>> [PHASE 6 COMPLETE] Ring 3 User Mode, Address Spaces & Syscalls verified.\n\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    /* 21. Initialize Virtual File System (VFS) & Mount Root RAMFS */
    vfs_init();
    if (ramfs_mount("/") != 0) {
        panic("Failed to initialize root RAMFS");
    }
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[OK] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("VFS initialized (Root mounted on RAMFS)\n");

    /* 22. Initialize and Mount Initial Ramdisk (Initrd) */
    if (initrd_init(mbd) != 0) {
        panic("Failed to parse or mount initial ramdisk");
    }
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("[OK] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    printk("Initrd mounted under /initrd (%u files loaded)\n", initrd_get_file_count());

    /* 23. Run automated Phase 7 subsystem test suite */
    run_phase7_tests();

    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk(">>> [PHASE 7 COMPLETE] Virtual File System & Ramdisk verified.\n");
    printk(">>> Type 'help' for commands, 'ls', 'pwd', 'cd', 'cat', 'touch', 'mkdir', 'rm', 'write', 'stat', 'tree'.\n\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    /* Interactive Command Loop */
    char cmd_buf[128];
    size_t cmd_len = 0;

    print_prompt();

    while (1) {
        char c = 0;
        if (keyboard_has_char()) {
            c = keyboard_getchar_nonblock();
        } else if (serial_received(COM1_PORT)) {
            c = serial_read(COM1_PORT);
            if (c == '\r') c = '\n';
        }

        if (c != 0) {
            if (c == '\n') {
                vga_putc('\n');
                serial_putc(COM1_PORT, '\r');
                serial_putc(COM1_PORT, '\n');

                cmd_buf[cmd_len] = '\0';
                if (cmd_len > 0) {
                    if (str_equals(cmd_buf, "mem")) {
                        mem_dump_stats();
                    } else if (str_equals(cmd_buf, "ps")) {
                        process_list();
                    } else if (str_equals(cmd_buf, "sched")) {
                        scheduler_print_stats();
                    } else if (str_equals(cmd_buf, "sched rr")) {
                        scheduler_set_policy(SCHED_POLICY_RR);
                        printk("Switched scheduling policy to Round Robin\n\n");
                    } else if (str_equals(cmd_buf, "sched priority")) {
                        scheduler_set_policy(SCHED_POLICY_PRIORITY);
                        printk("Switched scheduling policy to Priority (with Aging)\n\n");
                    } else if (str_equals(cmd_buf, "sched mlfq")) {
                        scheduler_set_policy(SCHED_POLICY_MLFQ);
                        printk("Switched scheduling policy to Multilevel Feedback Queue (MLFQ)\n\n");
                    } else if (str_equals(cmd_buf, "bench")) {
                        run_scheduler_benchmarks();
                    } else if (str_equals(cmd_buf, "uproc")) {
                        thread_t* u = process_create_user("u_shell", user_prog_basic, user_prog_basic_size);
                        if (u) {
                            printk("Spawned Ring 3 user process (TID %d, PID %d, CR3 %p)\n\n",
                                   u->tid, u->pid, (void*)u->cr3);
                        } else {
                            printk("Failed to spawn user process: out of memory\n\n");
                        }
                    } else if (str_equals(cmd_buf, "pwd")) {
                        shell_cmd_pwd();
                    } else if (str_starts_with(cmd_buf, "echo ")) {
                        printk("%s\\n\\n", skip_spaces(cmd_buf + 5));
                    } else if (str_equals(cmd_buf, "ls")) {
                        shell_cmd_ls(NULL);
                    } else if (str_starts_with(cmd_buf, "ls ")) {
                        shell_cmd_ls(skip_spaces(cmd_buf + 3));
                    } else if (str_equals(cmd_buf, "cd")) {
                        shell_cmd_cd(NULL);
                    } else if (str_starts_with(cmd_buf, "cd ")) {
                        shell_cmd_cd(skip_spaces(cmd_buf + 3));
                    } else if (str_starts_with(cmd_buf, "cat ")) {
                        shell_cmd_cat(skip_spaces(cmd_buf + 4));
                    } else if (str_starts_with(cmd_buf, "touch ")) {
                        shell_cmd_touch(skip_spaces(cmd_buf + 6));
                    } else if (str_starts_with(cmd_buf, "mkdir ")) {
                        shell_cmd_mkdir(skip_spaces(cmd_buf + 6));
                    } else if (str_starts_with(cmd_buf, "rm ")) {
                        shell_cmd_rm(skip_spaces(cmd_buf + 3));
                    } else if (str_starts_with(cmd_buf, "write ")) {
                        shell_cmd_write(skip_spaces(cmd_buf + 6));
                    } else if (str_starts_with(cmd_buf, "stat ")) {
                        shell_cmd_stat(skip_spaces(cmd_buf + 5));
                    } else if (str_equals(cmd_buf, "tree")) {
                        shell_cmd_tree(NULL);
                    } else if (str_starts_with(cmd_buf, "tree ")) {
                        shell_cmd_tree(skip_spaces(cmd_buf + 5));
                    } else if (str_equals(cmd_buf, "help")) {
                        printk("KernelX Shell Commands:\n");
                        printk("  System & Diagnostics:\n");
                        printk("    mem            - Display PMM, VMM, and Kernel Heap status\n");
                        printk("    ps             - List all processes and kernel threads\n");
                        printk("    sched          - Display scheduler telemetry and switch stats\n");
                        printk("    sched [rr|priority|mlfq] - Change active scheduling algorithm\n");
                        printk("    bench          - Run scheduler benchmarks\n");
                        printk("    uproc          - Spawn an isolated Ring 3 user process\n");
                        printk("    clear          - Clear the console screen\n");
                        printk("  Filesystem Commands:\n");
                        printk("    ls [path]      - List directory contents\n");
                        printk("    pwd            - Print current working directory\n");
                        printk("    cd [path]      - Change working directory (default: /)\n");
                        printk("    echo <text>    - Print text to the console\n");
                        printk("    cd <path>      - Change working directory\n");
                        printk("    cat <file>     - Display file contents\n");
                        printk("    touch <file>   - Create an empty file\n");
                        printk("    mkdir <dir>    - Create a new directory\n");
                        printk("    rm <path>      - Delete a file or directory\n");
                        printk("    write <f> <txt>- Write text to file\n");
                        printk("    stat <path>    - Display file/directory metadata\n");
                        printk("    tree [path]    - Display directory tree hierarchy\n");
                        printk("    help           - Display this command reference\n\n");
                    } else if (str_equals(cmd_buf, "clear")) {
                        vga_clear();
                    } else {
                        printk("Command not recognized: '%s'. Type 'help' for available commands.\n\n", cmd_buf);
                    }
                }
                cmd_len = 0;
                print_prompt();
            } else if (c == '\b') {
                if (cmd_len > 0) {
                    cmd_len--;
                    vga_putc('\b');
                    serial_putc(COM1_PORT, '\b');
                }
            } else if (c >= 32 && c <= 126) {
                if (cmd_len < sizeof(cmd_buf) - 1) {
                    cmd_buf[cmd_len++] = c;
                    vga_putc(c);
                    serial_putc(COM1_PORT, c);
                } else {
                    serial_putc(COM1_PORT, '\a');
                }
            }
        }
        thread_yield();
    }
}
