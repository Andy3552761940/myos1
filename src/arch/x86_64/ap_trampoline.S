.section .text
.code16
.global ap_trampoline_start
.global ap_trampoline_end
.global ap_trampoline_data

.set AP_DATA_STACK, 0
.set AP_DATA_CPU_ID, 8
.set AP_DATA_ENTRY, 16
.set AP_DATA_CR3, 24

ap_trampoline_start:
    cli
    movw %cs, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    movw %cs, %ax
    movzwl %ax, %eax
    shll $4, %eax
    movl %eax, %ebx

    movl %ebx, %eax
    addl $ap_gdt, %eax
    movl %eax, ap_gdt_ptr + 2

    movl %ebx, %eax
    addl $ap_prot, %eax
    movl %eax, ap_prot_ptr
    movw $0x08, ap_prot_ptr + 4

    movl %ebx, %eax
    addl $ap_long, %eax
    movl %eax, ap_long_ptr
    movw $0x18, ap_long_ptr + 4

    lgdt ap_gdt_ptr

    movl %cr0, %eax
    orl $0x1, %eax
    movl %eax, %cr0
    ljmp *ap_prot_ptr

.code32
ap_prot:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    movl ap_trampoline_data + AP_DATA_CR3, %eax
    movl %eax, %cr3

    movl %cr4, %eax
    orl $0x20, %eax
    movl %eax, %cr4

    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr

    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0

    ljmp *ap_long_ptr

.code64
ap_long:
    movw $0x20, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    movq ap_trampoline_data + AP_DATA_STACK, %rsp
    movq ap_trampoline_data + AP_DATA_ENTRY, %rax
    movq ap_trampoline_data + AP_DATA_CPU_ID, %rdi
    call *%rax

ap_hang:
    hlt
    jmp ap_hang

.align 8
ap_gdt:
    .quad 0
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
    .quad 0x00AF9A000000FFFF
    .quad 0x00CF92000000FFFF

ap_gdt_ptr:
    .word (ap_gdt_end - ap_gdt - 1)
    .long 0
ap_gdt_end:

ap_prot_ptr:
    .long 0
    .word 0

ap_long_ptr:
    .long 0
    .word 0

.align 8
ap_trampoline_data:
    .quad 0
    .quad 0
    .quad 0
    .quad 0

ap_trampoline_end:

.section .note.GNU-stack,"",@progbits
