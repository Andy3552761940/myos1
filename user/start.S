.section .text
.global _start
.type _start, @function
_start:
    /* Ensure user data segments are loaded (DS/ES not changed by IRET in long mode). */
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es

    xorq %rbp, %rbp
    /* RSP is already 16-byte aligned by the kernel. */

    call main

    /* exit(main_ret) */
    movq %rax, %rdi
    movq $2, %rax
    int $0x80

.hang:
    hlt
    jmp .hang

.section .note.GNU-stack,"",@progbits
