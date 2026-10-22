/*
 * link.ld — 内核链接脚本
 *
 * .text/.rodata/.data @ 1MiB；.bss @ 32MiB。
 * 任务栈加厚后若 BSS 仍挨着低端，单段跨度会撞 OVMF 低端保留页，
 * Boot AllocateAddress(整跨度) 失败。BSS 独立 PT_LOAD + Boot 按段申请。
 */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(KernelEntry)

PHDRS
{
    text PT_LOAD FLAGS(5); /* PF_R|PF_X */
    data PT_LOAD FLAGS(6); /* PF_R|PF_W */
    bss  PT_LOAD FLAGS(6); /* PF_R|PF_W；高位 BSS */
}

SECTIONS
{
    . = 0x100000;
    __kernel_start = .;

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

    /* 勿写 *(.bss*)——会吞掉 .bss.high */
    .bss : {
        *(.bss)
        *(COMMON)
    } :data
    __kernel_data_end = .;

    /*
     * 大块缓冲（XHCI scratch / PMM 段0 表）上抬到 32MiB：
     * 低端 BSS 若与 32K×MAX_TASKS 粘连，装载跨度越过 OVMF ~8MiB 保留区。
     * Boot 按 PT_LOAD 分段 AllocateAddress。
     */
    . = 0x2000000;
    __kernel_bss_start = .;
    .bss.high : {
        *(.bss.high)
    } :bss

    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
