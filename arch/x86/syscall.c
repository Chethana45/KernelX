#include <arch/x86/syscall.h>
#include <arch/x86/isr.h>
#include <arch/x86/pit.h>
#include <mm/vmm.h>
#include <sched/process.h>
#include <fs/vfs.h>
#include <drivers/vga.h>
#include <drivers/serial.h>
#include <drivers/keyboard.h>
#include <kernel/printk.h>

static int safe_copy_user_path(char* dest, const char* user_path, size_t max_len) {
    if (!user_path) return SYSCALL_EFAULT;
    thread_t* curr = thread_get_current();
    if (curr && curr->is_user) {
        if (!vmm_validate_user_ptr(user_path, 1, false)) return SYSCALL_EFAULT;
    }

    size_t i = 0;
    while (i < max_len - 1) {
        if (curr && curr->is_user) {
            if (!vmm_validate_user_ptr(user_path + i, 1, false)) return SYSCALL_EFAULT;
        }
        char c = user_path[i];
        dest[i++] = c;
        if (c == '\0') return 0;
    }
    dest[max_len - 1] = '\0';
    return SYSCALL_ENAMETOOLONG;
}

void syscall_dispatcher(registers_t* regs) {
    uint32_t syscall_num = regs->eax;
    thread_t* curr = thread_get_current();

    switch (syscall_num) {
        case SYS_EXIT: {
            int status = (int)regs->ebx;
            (void)status;
            thread_exit();
            /* Should never return */
            break;
        }

        case SYS_YIELD: {
            thread_yield();
            regs->eax = SYSCALL_SUCCESS;
            break;
        }

        case SYS_WRITE: {
            int fd = (int)regs->ebx;
            const char* buf = (const char*)regs->ecx;
            size_t count = (size_t)regs->edx;

            if (!curr || fd < 0 || fd >= MAX_PROCESS_FDS) {
                regs->eax = SYSCALL_EBADF;
                break;
            }

            if (curr->is_user && !vmm_validate_user_ptr(buf, count, false)) {
                regs->eax = SYSCALL_EFAULT;
                break;
            }

            if (count == 0) {
                regs->eax = 0;
                break;
            }

            if (fd == 1 || fd == 2) {
                /* Stdout / Stderr Dual-Sink Console Output */
                for (size_t i = 0; i < count; i++) {
                    char ch = buf[i];
                    vga_putc(ch);
                    if (ch == '\n') {
                        serial_putc(COM1_PORT, '\r');
                    }
                    serial_putc(COM1_PORT, ch);
                }
                regs->eax = count;
                break;
            } else if (fd == 0) {
                regs->eax = SYSCALL_EBADF;
                break;
            } else {
                /* VFS File Descriptor Write */
                file_descriptor_t* desc = process_fd_get(curr, fd);
                if (!desc || !desc->node) {
                    regs->eax = SYSCALL_EBADF;
                    break;
                }
                if ((desc->flags & 3u) == VFS_O_RDONLY) {
                    regs->eax = SYSCALL_EBADF;
                    break;
                }

                int32_t written = vfs_write(desc->node, desc->offset, count, (const uint8_t*)buf);
                if (written > 0) {
                    desc->offset += written;
                }
                regs->eax = written;
                break;
            }
        }

        case SYS_READ: {
            int fd = (int)regs->ebx;
            char* buf = (char*)regs->ecx;
            size_t count = (size_t)regs->edx;

            if (!curr || fd < 0 || fd >= MAX_PROCESS_FDS) {
                regs->eax = SYSCALL_EBADF;
                break;
            }

            if (curr->is_user && !vmm_validate_user_ptr(buf, count, true)) {
                regs->eax = SYSCALL_EFAULT;
                break;
            }

            if (count == 0) {
                regs->eax = 0;
                break;
            }

            if (fd == 0) {
                /* Stdin Keyboard Input */
                size_t read_bytes = 0;
                while (read_bytes < count && keyboard_has_char()) {
                    buf[read_bytes++] = keyboard_getchar_nonblock();
                }
                regs->eax = read_bytes;
                break;
            } else if (fd == 1 || fd == 2) {
                regs->eax = SYSCALL_EBADF;
                break;
            } else {
                /* VFS File Descriptor Read */
                file_descriptor_t* desc = process_fd_get(curr, fd);
                if (!desc || !desc->node) {
                    regs->eax = SYSCALL_EBADF;
                    break;
                }
                if ((desc->flags & 3u) == VFS_O_WRONLY) {
                    regs->eax = SYSCALL_EBADF;
                    break;
                }

                int32_t read_bytes = vfs_read(desc->node, desc->offset, count, (uint8_t*)buf);
                if (read_bytes > 0) {
                    desc->offset += read_bytes;
                }
                regs->eax = read_bytes;
                break;
            }
        }

        case SYS_GETPID: {
            regs->eax = curr ? (uint32_t)curr->pid : 0;
            break;
        }

        case SYS_SLEEP: {
            uint32_t ticks = (uint32_t)regs->ebx;
            pit_sleep(ticks);
            regs->eax = SYSCALL_SUCCESS;
            break;
        }

        case SYS_OPEN: {
            const char* u_path = (const char*)regs->ebx;
            int flags = (int)regs->ecx;
            int mode = (int)regs->edx;

            if (!curr) {
                regs->eax = SYSCALL_EINVAL;
                break;
            }

            char kpath[VFS_PATH_MAX];
            int cp_res = safe_copy_user_path(kpath, u_path, VFS_PATH_MAX);
            if (cp_res != 0) {
                regs->eax = cp_res;
                break;
            }

            vfs_node_t* node = vfs_resolve_path(kpath, curr->cwd);
            if (!node) {
                if (flags & VFS_O_CREAT) {
                    if (vfs_create(kpath, curr->cwd, mode) == 0) {
                        node = vfs_resolve_path(kpath, curr->cwd);
                    }
                }
            }

            if (!node) {
                regs->eax = SYSCALL_ENOENT;
                break;
            }

            if ((flags & VFS_O_TRUNC) != 0) {
                if ((flags & 3u) == VFS_O_RDONLY || vfs_truncate(node) != 0) {
                    regs->eax = SYSCALL_EINVAL;
                    break;
                }
            }

            if (vfs_open(node, flags) != 0) {
                regs->eax = SYSCALL_EINVAL;
                break;
            }

            int fd = process_fd_alloc(curr, node, flags);
            if (fd < 0) {
                vfs_close(node);
                regs->eax = -24; /* EMFILE */
                break;
            }
            regs->eax = fd;
            break;
        }

        case SYS_CLOSE: {
            int fd = (int)regs->ebx;
            if (!curr || fd < 0 || fd >= MAX_PROCESS_FDS) {
                regs->eax = SYSCALL_EBADF;
                break;
            }

            if (fd == 0 || fd == 1 || fd == 2) {
                regs->eax = SYSCALL_SUCCESS;
                break;
            }

            if (process_fd_free(curr, fd) != 0) {
                regs->eax = SYSCALL_EBADF;
                break;
            }

            regs->eax = SYSCALL_SUCCESS;
            break;
        }

        case SYS_MKDIR: {
            const char* u_path = (const char*)regs->ebx;
            int mode = (int)regs->ecx;

            if (!curr) {
                regs->eax = SYSCALL_EINVAL;
                break;
            }

            char kpath[VFS_PATH_MAX];
            int cp_res = safe_copy_user_path(kpath, u_path, VFS_PATH_MAX);
            if (cp_res != 0) {
                regs->eax = cp_res;
                break;
            }

            int res = vfs_mkdir(kpath, curr->cwd, mode);
            regs->eax = (res == 0) ? SYSCALL_SUCCESS : SYSCALL_ENOENT;
            break;
        }

        case SYS_UNLINK: {
            const char* u_path = (const char*)regs->ebx;

            if (!curr) {
                regs->eax = SYSCALL_EINVAL;
                break;
            }

            char kpath[VFS_PATH_MAX];
            int cp_res = safe_copy_user_path(kpath, u_path, VFS_PATH_MAX);
            if (cp_res != 0) {
                regs->eax = cp_res;
                break;
            }

            int res = vfs_unlink(kpath, curr->cwd);
            regs->eax = (res == 0) ? SYSCALL_SUCCESS : SYSCALL_ENOENT;
            break;
        }

        case SYS_STAT: {
            const char* u_path = (const char*)regs->ebx;
            vfs_stat_t* u_stat = (vfs_stat_t*)regs->ecx;

            if (!curr) {
                regs->eax = SYSCALL_EINVAL;
                break;
            }

            char kpath[VFS_PATH_MAX];
            int cp_res = safe_copy_user_path(kpath, u_path, VFS_PATH_MAX);
            if (cp_res != 0) {
                regs->eax = cp_res;
                break;
            }

            if (!u_stat || (curr->is_user && !vmm_validate_user_ptr(u_stat, sizeof(vfs_stat_t), true))) {
                regs->eax = SYSCALL_EFAULT;
                break;
            }

            vfs_stat_t kstat;
            int res = vfs_stat(kpath, curr->cwd, &kstat);
            if (res == 0) {
                *u_stat = kstat;
                regs->eax = SYSCALL_SUCCESS;
            } else {
                regs->eax = SYSCALL_ENOENT;
            }
            break;
        }

        case SYS_READDIR: {
            int fd = (int)regs->ebx;
            vfs_dirent_t* u_dirent = (vfs_dirent_t*)regs->ecx;

            if (!curr || fd < 3 || fd >= MAX_PROCESS_FDS) {
                regs->eax = SYSCALL_EBADF;
                break;
            }

            if (!u_dirent || (curr->is_user && !vmm_validate_user_ptr(u_dirent, sizeof(vfs_dirent_t), true))) {
                regs->eax = SYSCALL_EFAULT;
                break;
            }

            file_descriptor_t* desc = process_fd_get(curr, fd);
            if (!desc || !desc->node) {
                regs->eax = SYSCALL_EBADF;
                break;
            }

            vfs_dirent_t kdirent;
            int res = vfs_readdir(desc->node, desc->offset, &kdirent);
            if (res == 0) {
                desc->offset++;
                *u_dirent = kdirent;
                regs->eax = 1;
            } else {
                regs->eax = 0; /* EOF */
            }
            break;
        }

        default:
            regs->eax = SYSCALL_EINVAL;
            break;
    }
}

void syscall_init(void) {
    isr_register_handler(128, syscall_dispatcher);
}
