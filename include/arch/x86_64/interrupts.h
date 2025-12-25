#pragma once
#include <stdint.h>

/*
 * Interrupt frame layout as built by interrupts.S:
 *
 *  - For all interrupts/exceptions: CPU pushes RIP, CS, RFLAGS
 *  - If CPL change (user->kernel): CPU also pushes RSP, SS
 *  - If exception with error code: CPU pushes error code (64-bit in long mode)
 *
 * Our stubs then push:
 *    (if no error code) dummy error code = 0
 *    interrupt vector number
 * and then the general registers in this order:
 *    rax, rbx, rcx, rdx, rbp, rdi, rsi, r8, r9, r10, r11, r12, r13, r14, r15
 *
 * After pushes, RSP points to r15 (top of this struct).
 */
typedef struct intr_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rsi, rdi, rbp, rdx, rcx, rbx, rax;

    uint64_t int_no;
    uint64_t err_code;

    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;

    /* Only present if (cs & 3) == 3 */
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) intr_frame_t;

/* Called from assembly; may return a different frame pointer to resume. */
intr_frame_t* interrupt_dispatch(intr_frame_t* frame);

/* These are implemented in assembly. */
void isr_enable(void);
void isr_disable(void);
