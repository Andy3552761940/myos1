#pragma once
#include <stdint.h>
#include "arch/x86_64/interrupts.h"
#include "thread.h"

void scheduler_init(void);
void scheduler_add(thread_t* t);

/* Create a kernel thread that runs fn(arg). */
thread_t* thread_create_kernel(const char* name, void (*fn)(void*), void* arg);

/* Create a user thread that starts at user_rip with a fresh user stack. */
thread_t* thread_create_user(const char* name, uint64_t user_rip);

/* Called from timer IRQ handler; returns frame to resume. */
intr_frame_t* scheduler_on_tick(intr_frame_t* frame);

/* Called when a thread exits (syscall or fault). Returns next frame to resume. */
intr_frame_t* scheduler_on_exit(intr_frame_t* frame, int exit_code);

/* Cooperative yield (invoked by syscall yield). */
intr_frame_t* scheduler_yield(intr_frame_t* frame);

/* Sleep current thread for n ticks (called from kernel code only). */
void scheduler_sleep(uint64_t ticks);

/* Print scheduler status. */
void scheduler_dump(void);
