/*
 * link.ld — QEMU virt aarch64（-kernel ELF）
 * ENTRY = KernelEntry（Boot/Arm64/Boot.S）
 */
OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(KernelEntry)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x40000000;

    .text : {
        KEEP(*(.text.boot))
        *(.text*)
    } :text

    .rodata : {
        *(.rodata*)
    } :text

    . = ALIGN(0x1000);
    .data : {
        *(.data*)
    } :data

    .bss : {
        __bss_start = .;
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
