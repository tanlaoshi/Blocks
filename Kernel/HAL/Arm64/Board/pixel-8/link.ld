/*
 * link.ld — Pixel 8（ABL 加载 Linux Image @0x90701000）
 *
 * 对表真机串口：[AND] kernel addr phys 0x90701000（见 BoardConfig）。
 * 入口带 ARM64 Image 头（KernelEntry.S + BOARD_LINUX_IMAGE_HEADER）。
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
    . = 0x90701000;

    .text : {
        KEEP(*(.text.boot))
        *(.text*)
    } :text

    .rodata : {
        __driver_boot_start = .;
        KEEP(*(.toy.driver.boot))
        __driver_boot_end = .;
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
