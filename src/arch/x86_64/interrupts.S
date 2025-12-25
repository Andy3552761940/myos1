.section .text
.code64

.global isr_enable
.global isr_disable
isr_enable:
    sti
    ret
isr_disable:
    cli
    ret

.extern interrupt_dispatch

/* Common stub for all interrupts/exceptions/IRQs/syscalls. */
.global isr_common_stub
isr_common_stub:
    /* Save general purpose registers (SysV: preserve everything). */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* Call C dispatcher: intr_frame_t* interrupt_dispatch(intr_frame_t* frame) */
    movq %rsp, %rdi

    /* Align stack for the call (16-byte aligned before CALL). */
    movq %rsp, %rax
    andq $0xF, %rax
    movq %rax, %rbx
    subq %rax, %rsp

    call interrupt_dispatch

    addq %rbx, %rsp

    /* Switch to returned frame if different. */
    movq %rax, %rsp

    /* Restore general purpose registers. */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rsi
    popq %rdi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    /* Drop int_no and err_code */
    addq $16, %rsp
    iretq

/* Macros to define ISR stubs */
.macro ISR_NOERR n
.global isr\n
isr\n:
    pushq $0
    pushq $\n
    jmp isr_common_stub
.endm

.macro ISR_ERR n
.global isr\n
isr\n:
    pushq $\n
    jmp isr_common_stub
.endm

.macro IRQ n vec
.global irq\n
irq\n:
    pushq $0
    pushq $\vec
    jmp isr_common_stub
.endm

/* CPU exceptions */
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

/* Hardware IRQs (PIC remapped) */
IRQ 0 32
IRQ 1 33
IRQ 2 34
IRQ 3 35
IRQ 4 36
IRQ 5 37
IRQ 6 38
IRQ 7 39
IRQ 8 40
IRQ 9 41
IRQ 10 42
IRQ 11 43
IRQ 12 44
IRQ 13 45
IRQ 14 46
IRQ 15 47

/* Generic ISRs for vectors 48-127 and 129-255 */
.altmacro
.set i, 48
.rept 80
ISR_NOERR %i
.set i, i+1
.endr
.set i, 129
.rept 127
ISR_NOERR %i
.set i, i+1
.endr
.noaltmacro

/* Syscall vector 0x80 */
.global isr128
isr128:
    pushq $0
    pushq $128
    jmp isr_common_stub

.section .rodata
.global isr_stub_table
isr_stub_table:
    .include "src/arch/x86_64/isr_stub_table.inc"

.section .note.GNU-stack,"",@progbits
