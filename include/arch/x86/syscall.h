#ifndef ARCH_X86_SYSCALL_H
#define ARCH_X86_SYSCALL_H

#include <kernel/types.h>
#include <arch/x86/isr.h>

/* System Call Identifiers */
#define SYS_EXIT    1
#define SYS_YIELD   2
#define SYS_WRITE   3
#define SYS_READ    4
#define SYS_GETPID  5
#define SYS_SLEEP   6
#define SYS_OPEN    7
#define SYS_CLOSE   8
#define SYS_MKDIR   9
#define SYS_UNLINK  10
#define SYS_STAT    11
#define SYS_READDIR 12

/* Error codes */
#define SYSCALL_SUCCESS       0
#define SYSCALL_ENOENT      (-2)
#define SYSCALL_EBADF       (-9)
#define SYSCALL_EFAULT      (-14)
#define SYSCALL_EEXIST      (-17)
#define SYSCALL_EINVAL      (-22)
#define SYSCALL_ENAMETOOLONG (-36)

void syscall_init(void);
void syscall_dispatcher(registers_t* regs);

/* User-space system call execution primitives */
static inline int syscall0(int num) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num) : "memory");
    return ret;
}

static inline int syscall1(int num, int arg1) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1) : "memory");
    return ret;
}

static inline int syscall2(int num, int arg1, int arg2) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2) : "memory");
    return ret;
}

static inline int syscall3(int num, int arg1, int arg2, int arg3) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3) : "memory");
    return ret;
}

/* User-space standard API wrappers */
static inline void sys_exit_u(int status) {
    syscall1(SYS_EXIT, status);
}

static inline void sys_yield_u(void) {
    syscall0(SYS_YIELD);
}

static inline int sys_write_u(int fd, const char* buf, size_t count) {
    return syscall3(SYS_WRITE, fd, (int)buf, (int)count);
}

static inline int sys_read_u(int fd, char* buf, size_t count) {
    return syscall3(SYS_READ, fd, (int)buf, (int)count);
}

static inline int sys_getpid_u(void) {
    return syscall0(SYS_GETPID);
}

static inline void sys_sleep_u(uint32_t ticks) {
    syscall1(SYS_SLEEP, (int)ticks);
}

static inline int sys_open_u(const char* path, int flags, int mode) {
    return syscall3(SYS_OPEN, (int)path, flags, mode);
}

static inline int sys_close_u(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

static inline int sys_mkdir_u(const char* path, int mode) {
    return syscall2(SYS_MKDIR, (int)path, mode);
}

static inline int sys_unlink_u(const char* path) {
    return syscall1(SYS_UNLINK, (int)path);
}

static inline int sys_stat_u(const char* path, void* stat_buf) {
    return syscall2(SYS_STAT, (int)path, (int)stat_buf);
}

static inline int sys_readdir_u(int fd, void* dirent_buf) {
    return syscall2(SYS_READDIR, fd, (int)dirent_buf);
}

#endif /* ARCH_X86_SYSCALL_H */
