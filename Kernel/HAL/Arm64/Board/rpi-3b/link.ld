/*
 * link.ld — Raspberry Pi 3B（BCM2837，~1GiB DRAM @0）
 *
 * 不可用 virt 的 0x40000000：真机 DRAM 约 948MiB，该址越界 → bootelf 写飞复位。
 * 与 BoardConfig.h BOARD_KERNEL_LOAD、常见 U-Boot kernel_addr_r 对齐。
 *
 * 加载：ELF 文件请放到与段址不重叠的暂存址再 bootelf，例如：
 *   load mmc 0:1 0x20000000 Kernel.elf
 *   bootelf -p 0x20000000
 */
OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(KernelEntry)

PHDRS
{
    text PT_LOAD FLAGS(5); /* PF_R|PF_X */
    data PT_LOAD FLAGS(6); /* PF_R|PF_W */
}

SECTIONS
{
    . = 0x00080000;

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
