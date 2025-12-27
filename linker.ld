OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
    text   PT_LOAD FLAGS(5); /* R + X */
    rodata PT_LOAD FLAGS(4); /* R */
    data   PT_LOAD FLAGS(6); /* R + W */
}

SECTIONS
{
    . = 1M;
    _kernel_start = .;

    .multiboot : ALIGN(8) {
        KEEP(*(.multiboot))
    } :text

    .text : ALIGN(0x1000) {
        *(.text*)
    } :text

    .ap_trampoline : ALIGN(16) {
        *(.ap_trampoline*)
    } :text

    .rodata : ALIGN(0x1000) {
        *(.rodata*)
    } :rodata

    .data : ALIGN(0x1000) {
        *(.data*)
    } :data

    .bss : ALIGN(0x1000) {
        *(COMMON)
        *(.bss*)
    } :data

    _kernel_end = .;
}
