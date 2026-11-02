/*
 * BootStub link.ld — 高地址第二阶段（FIT load/entry = 0x83000000）
 *
 * 躲开真核 @0x80200000、DTB @0x81200000、U-Boot elf 缓冲 @0x81800000。
 */
OUTPUT_FORMAT(elf64-littleriscv)
OUTPUT_ARCH(riscv)
ENTRY(BootStubEntry)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x83000000;

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
        gBootStubStack = .;
        . = . + 65536;
        gBootStubStackTop = .;
    } :data

    __boot_stub_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
