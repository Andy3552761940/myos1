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

intr_frame_t* syscall_handle(intr_frame_t* frame);
