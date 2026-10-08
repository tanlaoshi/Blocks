/*
 * link.ld — QEMU virt riscv64（OpenSBI payload @ 0x80200000）
 * ENTRY = KernelEntry（Boot/RiscV/Boot.S）
 */
OUTPUT_FORMAT(elf64-littleriscv)
OUTPUT_ARCH(riscv)
ENTRY(KernelEntry)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x80200000;

    .text : {
        KEEP(*(.text.boot))
        *(.text*)
    } :text

    .rodata : {
        *(.rodata*)
        *(.srodata*)
    } :text

    . = ALIGN(0x1000);
    .data : {
        *(.data*)
        *(.sdata*)
    } :data

    .bss : {
        __bss_start = .;
        *(.sbss*)
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } :data

    .bootstack (NOLOAD) : {
        . = ALIGN(16);
        gBootStack = .;
        . = . + 65536;
    } :data

    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
