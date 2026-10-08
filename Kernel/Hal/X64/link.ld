/*
 * link.ld — X64 Kernel（物理/虚拟 0x100000）
 * ENTRY = KernelEntry（EFI JumpToKernel 的目标；汇编设栈后再进 KernelHandoff）
 */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(KernelEntry)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x100000;

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
        *(.bss*)
        *(COMMON)
    } :data

    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
