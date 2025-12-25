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
    THREAD_BLOCKED,
    THREAD_ZOMBIE,
} thread_state_t;

typedef struct vfs_file vfs_file_t;

#define THREAD_MAX_OPEN_FILES 8

typedef struct thread {
    uint64_t id;
    char     name[16];

    thread_state_t state;
    bool     is_user;
    int      priority;

    /* Simple parent/child tracking for wait/exit. */
    struct thread* parent;
    uint32_t children;
    int      exit_code;

    /* Waiting information (used when state == THREAD_BLOCKED). */
    int      wait_target;
    uint64_t wait_status_ptr;

    /* Saved interrupt-frame stack pointer (points to r15 in intr_frame_t). */
    uint64_t rsp;

    /* Address space root (CR3) physical address; identity-mapped in this kernel. */
    uint64_t cr3;

    /* Kernel stack (always present). */
    uint8_t* kstack;
    size_t   kstack_size;
    uint64_t kstack_canary;

    /* User stack (only for user threads). */
    uint8_t* ustack;
    size_t   ustack_size;
    uint64_t ustack_top;

    /* User heap (brk). */
    uint64_t brk_start;
    uint64_t brk_end;

    /* Very small and simplistic file table placeholder. */
    vfs_file_t* open_files[THREAD_MAX_OPEN_FILES];
    size_t      open_file_count;

    /* Simple mmap base for anonymous mappings. */
    uint64_t mmap_base;

    uint64_t wakeup_tick;

    uint32_t cpu_id;

    /* For kernel thread trampoline */
    void (*kentry)(void*);
    void* karg;
} thread_t;

thread_t* thread_current(void);
void thread_kstack_canary_init(thread_t* t);
bool thread_kstack_canary_ok(const thread_t* t);
