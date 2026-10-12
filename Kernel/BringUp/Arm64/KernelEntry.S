/*
 * KernelEntry.S — Arm64 QEMU virt -kernel 入口（保留 x0=DTB）
 *
 * 与 X64 合流：换栈/清 BSS → KernelHandoff → BringUp/KernelMain。
 */
    .section .text.boot, "ax"
    .global KernelEntry
    .type KernelEntry, %function
KernelEntry:
    /* Linux/QEMU aarch64：x0 = DTB 物理址 */
    mov     x19, x0

    adrp    x0, gBootStack
    add     x0, x0, :lo12:gBootStack
    add     sp, x0, #65536

    adrp    x0, __bss_start
    add     x0, x0, :lo12:__bss_start
    adrp    x1, __bss_end
    add     x1, x1, :lo12:__bss_end
1:  cmp     x0, x1
    b.hs    2f
    str     xzr, [x0], #8
    b       1b
2:
    mov     x0, x19
    bl      KernelHandoff
3:  wfi
    b       3b
    .size KernelEntry, .-KernelEntry
