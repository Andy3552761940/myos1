#pragma once
#include <stdint.h>

#define SYS_write 1
#define SYS_exit  2
#define SYS_yield 3
#define SYS_brk   4
#define SYS_fork  5
#define SYS_execve 6
#define SYS_waitpid 7
#define SYS_gettimeofday 8
#define SYS_sleep 9
#define SYS_socket 10
#define SYS_bind 11
#define SYS_sendto 12
#define SYS_recvfrom 13
#define SYS_connect 14
#define SYS_listen 15
#define SYS_accept 16
#define SYS_close 17
#define SYS_open 18
#define SYS_read 19
#define SYS_lseek 20
#define SYS_getpid 21
#define SYS_uname 22
#define SYS_sysinfo 23
#define SYS_mmap 24
#define SYS_kill 25
#define SYS_readdir 26

#define SYS_SEEK_SET 0
#define SYS_SEEK_CUR 1
#define SYS_SEEK_END 2

#define O_RDONLY 0x1
#define O_WRONLY 0x2
#define O_RDWR   (O_RDONLY | O_WRONLY)
#define O_CREAT  0x4

#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

static inline int64_t sys_call3(int64_t num, int64_t a1, int64_t a2, int64_t a3) {
    int64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
        : "memory"
    );
    return ret;
}

static inline int64_t sys_call6(int64_t num, int64_t a1, int64_t a2, int64_t a3,
                                int64_t a4, int64_t a5, int64_t a6) {
    int64_t ret;
    register int64_t r10 __asm__("r10") = a4;
    register int64_t r8 __asm__("r8") = a5;
    register int64_t r9 __asm__("r9") = a6;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "memory"
    );
    return ret;
}

static inline int64_t sys_call1(int64_t num, int64_t a1) {
    int64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1)
        : "memory"
    );
    return ret;
}

static inline int64_t sys_yield(void) {
    return sys_call3(SYS_yield, 0, 0, 0);
}

static inline int64_t sys_write(int64_t fd, const void* buf, int64_t len) {
    return sys_call3(SYS_write, fd, (int64_t)(uintptr_t)buf, len);
}

static inline int64_t sys_brk(void* end) {
    return sys_call1(SYS_brk, (int64_t)(uintptr_t)end);
}

static inline int64_t sys_fork(void) {
    return sys_call1(SYS_fork, 0);
}

static inline int64_t sys_execve(const char* path) {
    return sys_call3(SYS_execve, (int64_t)(uintptr_t)path, 0, 0);
}

static inline int64_t sys_waitpid(int64_t pid, int* status) {
    return sys_call3(SYS_waitpid, pid, (int64_t)(uintptr_t)status, 0);
}

static inline int64_t sys_open(const char* path, int64_t flags) {
    return sys_call3(SYS_open, (int64_t)(uintptr_t)path, flags, 0);
}

static inline int64_t sys_read(int64_t fd, void* buf, int64_t len) {
    return sys_call3(SYS_read, fd, (int64_t)(uintptr_t)buf, len);
}

static inline int64_t sys_close(int64_t fd) {
    return sys_call1(SYS_close, fd);
}

static inline int64_t sys_readdir(int64_t fd, char* buf, int64_t len) {
    return sys_call3(SYS_readdir, fd, (int64_t)(uintptr_t)buf, len);
}

static inline int64_t sys_lseek(int64_t fd, int64_t offset, int64_t whence) {
    return sys_call3(SYS_lseek, fd, offset, whence);
}

static inline int64_t sys_getpid(void) {
    return sys_call1(SYS_getpid, 0);
}

static inline int64_t sys_uname(void* info) {
    return sys_call1(SYS_uname, (int64_t)(uintptr_t)info);
}

static inline int64_t sys_sysinfo(void* info) {
    return sys_call1(SYS_sysinfo, (int64_t)(uintptr_t)info);
}

static inline void* sys_mmap(void* addr, uint64_t len, int prot) {
    return (void*)(uintptr_t)sys_call6(SYS_mmap, (int64_t)(uintptr_t)addr,
                                       (int64_t)len, prot, 0, 0, 0);
}

static inline int64_t sys_kill(int64_t pid, int64_t sig) {
    return sys_call3(SYS_kill, pid, sig, 0);
}

static inline void sys_exit(int64_t code) {
    sys_call1(SYS_exit, code);
    for (;;) { __asm__ volatile("hlt"); }
}
