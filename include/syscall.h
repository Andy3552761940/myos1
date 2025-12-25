#pragma once
#include "arch/x86_64/interrupts.h"

/* Simple syscall numbers */
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

intr_frame_t* syscall_handle(intr_frame_t* frame);
