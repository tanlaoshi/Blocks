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

    __kernel_data_end = .;

    /*
     * PR-24：整个 BSS 上抬到 32MiB，低端只留 text/rodata/data。
     *
     * 原因：低端段从 1MiB 顺序往下排，而 OVMF 在约 8MiB 处有保留区，
     * Boot 又是按 PT_LOAD 整段 AllocateAddress —— 于是「data+bss」的总额
     * 被隐形钉死在约 7MiB。PR-23b 的 4MB 字形缓存正是撞在这里。把 BSS 整体
     * 搬上去之后这条预算从 7MiB 变成约 3GiB，`.bss.high` 标记已全部退休。
     *
     * 清零由 Boot 的 ELF 装载保证：每个 PT_LOAD 都 SetMem(SegBase,
     * SegPages*4096, 0) 再拷 SizeInFile，所以 filesz=0 的 BSS 段整段为 0。
     */
    . = 0x2000000;
    __kernel_bss_start = .;
    .bss : {
        *(.bss)
        *(COMMON)
    } :bss

    __kernel_end = .;

    /*
     * 护栏：低端 RW 段若再长到保留区，链接期就失败，而不是开机才报
     * AllocatePages Failed。
     */
    ASSERT(__kernel_data_end <= 0x800000,
           "kernel low RW segment crosses OVMF reserved region (~8MiB)")

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
