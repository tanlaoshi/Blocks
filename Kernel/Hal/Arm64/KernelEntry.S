/*
 * KernelEntry.S — Arm64 QEMU virt「-kernel」入口
 *
 * 【初学者 · 谁调用我们？】
 *   QEMU/加载器把整颗 Kernel.elf 放进内存后，跳到映像入口（链接脚本把
 *   KernelEntry 放在最前）。此时已是 EL1（或约定级别），DTB 指针在 x0。
 *
 * 本文件只做三件事（C 里再组 BOOT_INFO）：
 *   1) 保住 x0（DTB 物理址）——后面清 BSS / 设栈会弄脏调用约定寄存器；
 *   2) 架好临时栈 gBootStack，并把 .bss 清零；
 *   3) bl KernelHandoff(x0=DTB)，返回后死循环 wfi（正常不应返回）。
 *
 * 与 X64 的差别：没有 UEFI、不自己读盘、不 ExitBootServices。
 * 与 RiscV 的差别：单入口、无 hart 竞选；多核由后续 HAL SMP 处理。
 */
    .section .text.boot, "ax"
    .global KernelEntry
    .type KernelEntry, %function
KernelEntry:
    /*
     * Linux/QEMU aarch64 boot protocol：x0 = DTB 物理地址。
     * 先挪到 callee-saved x19，避免设栈/清 BSS 时弄丢。
     */
    mov     x19, x0

    /* SP = gBootStack + 64KiB（栈向低地址增长，指向缓冲区高端） */
    adrp    x0, gBootStack
    add     x0, x0, :lo12:gBootStack
    add     sp, x0, #65536

    /*
     * 清 BSS：链接脚本提供 __bss_start / __bss_end。
     * KernelHandoff 里的 static BOOT_INFO 等依赖这里为零。
     */
    adrp    x0, __bss_start
    add     x0, x0, :lo12:__bss_start
    adrp    x1, __bss_end
    add     x1, x1, :lo12:__bss_end
1:  cmp     x0, x1
    b.hs    2f
    str     xzr, [x0], #8
    b       1b
2:
    /* 恢复 DTB 参数，进入 C：void KernelHandoff(UINT64 DtbPhys) */
    mov     x0, x19
    bl      KernelHandoff

    /* KernelHandoff / KernelMain 若返回：停在此，避免跑飞 */
3:  wfi
    b       3b
    .size KernelEntry, .-KernelEntry
