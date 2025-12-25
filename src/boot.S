.section .multiboot
.align 8
/* Multiboot2 header */
multiboot2_header:
    .long 0xE85250D6          /* magic */
    .long 0                   /* architecture (i386) */
    .long multiboot2_header_end - multiboot2_header  /* header length */
    .long -(0xE85250D6 + 0 + (multiboot2_header_end - multiboot2_header)) /* checksum */

/* End tag */
    .short 0
    .short 0
    .long 8

multiboot2_header_end:

.section .text
.code32
.global _start
_start:
    cli

    /* Save multiboot2 registers */
    movl %eax, mb2_magic
    movl %ebx, mb2_info

    /* Set up a temporary stack for early boot work (still in 32-bit mode). */
    movl $stack_top, %esp


    /* Build identity-mapped 2MiB paging structures for 0..4GiB (user-accessible). */
    movl $pd_table0, %edi
    movl $0x00000000, %ebx
    call fill_pd_table
    movl $pd_table1, %edi
    movl $0x40000000, %ebx /* 1GiB */
    call fill_pd_table
    movl $pd_table2, %edi
    movl $0x80000000, %ebx /* 2GiB */
    call fill_pd_table
    movl $pd_table3, %edi
    movl $0xC0000000, %ebx /* 3GiB */
    call fill_pd_table
    /* Load a minimal GDT (32-bit protected mode) */
    lgdt gdt_ptr32

    /* Enable PAE */
    movl %cr4, %eax
    orl  $0x20, %eax
    movl %eax, %cr4

    /* Load PML4 into CR3 */
    movl $pml4_table, %eax
    movl %eax, %cr3

    /* Enable long mode (EFER.LME) */
    movl $0xC0000080, %ecx
    rdmsr
    orl  $0x100, %eax
    wrmsr

    /* Enable paging (CR0.PG) */
    movl %cr0, %eax
    orl  $0x80000000, %eax
    movl %eax, %cr0

    /* Far jump to 64-bit code segment */
    ljmp $0x08, $long_mode_start


/* Fill a PD table at %edi with 512 2MiB entries starting at physical base %ebx.
   Flags: P|W|U|PS (0x087). High dword stays 0 (tables are zero-initialized). */
fill_pd_table:
    pushl %ebp
    movl %esp, %ebp
    pushl %ecx
    pushl %eax

    xorl %ecx, %ecx
1:
    movl %ebx, %eax
    orl  $0x087, %eax
    movl %eax, (%edi)
    addl $8, %edi
    addl $0x200000, %ebx
    incl %ecx
    cmpl $512, %ecx
    jne 1b

    popl %eax
    popl %ecx
    popl %ebp
    ret

.code64
long_mode_start:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    lea stack_top(%rip), %rsp

    /* Load multiboot args for C: (uint64_t magic, const mb2_info_t* info) */
    movl mb2_magic(%rip), %edi
    movl mb2_info(%rip), %esi

    /* Clear RBP to make backtraces nicer */
    xorq %rbp, %rbp

    call kernel_main

.hang:
    hlt
    jmp .hang

/* 32-bit GDT: null, code, data */
.align 16
gdt32:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF
    .quad 0x00CF92000000FFFF

gdt_ptr32:
    .word (gdt32_end - gdt32 - 1)
    .long gdt32
gdt32_end:

.section .bss
.align 8
mb2_magic:
    .long 0
mb2_info:
    .long 0

/* Page tables (aligned to 4KiB) */
.align 4096
pml4_table:
    .fill 512,8,0

.align 4096
pdpt_table:
    .fill 512,8,0

.align 4096
pd_table0:
    .fill 512,8,0
.align 4096
pd_table1:
    .fill 512,8,0
.align 4096
pd_table2:
    .fill 512,8,0
.align 4096
pd_table3:
    .fill 512,8,0

/* Boot stack */
.align 16
.global stack_bottom
.global stack_top
stack_bottom:
    .skip 16384
stack_top:

.section .note.GNU-stack,"",@progbits
