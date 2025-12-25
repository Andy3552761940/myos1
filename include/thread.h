#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "arch/x86_64/interrupts.h"

typedef enum {
    THREAD_UNUSED = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_SLEEPING,
    THREAD_ZOMBIE,
} thread_state_t;

typedef struct thread {
    uint64_t id;
    char     name[16];

    thread_state_t state;
    bool     is_user;

    /* Saved interrupt-frame stack pointer (points to r15 in intr_frame_t). */
    uint64_t rsp;

    /* Address space root (CR3) physical address; identity-mapped in this kernel. */
    uint64_t cr3;

    /* Kernel stack (always present). */
    uint8_t* kstack;
    size_t   kstack_size;

    /* User stack (only for user threads). */
    uint8_t* ustack;
    size_t   ustack_size;

    uint64_t wakeup_tick;

    /* For kernel thread trampoline */
    void (*kentry)(void*);
    void* karg;
} thread_t;

thread_t* thread_current(void);
