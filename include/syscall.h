#pragma once
#include "arch/x86_64/interrupts.h"

/* Simple syscall numbers */
#define SYS_write 1
#define SYS_exit  2
#define SYS_yield 3

intr_frame_t* syscall_handle(intr_frame_t* frame);
