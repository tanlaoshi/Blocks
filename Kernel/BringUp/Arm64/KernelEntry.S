/*
 * KernelEntry.S — Arm64 入口（保留 x0=DTB）
 *
 * .S 经 cpp：可 #include BoardConfig.h。
 * Pi mini-UART：BSS 清零前先打字符，避免「Starting application」后长时间无字。
 */
#include "BoardConfig.h"

    .section .text.boot, "ax"
    .global KernelEntry
    .type KernelEntry, %function
KernelEntry:
    /* Linux/U-Boot aarch64：x0 = DTB 物理址 */
    mov     x19, x0

#if BOARD_UART_MINI
    /* AUX @0x3F215000；IO=+0x40 LSR=+0x54 bit5=TX empty */
    movz    x0, #0x5000
    movk    x0, #0x3f21, lsl #16
10: ldr     w1, [x0, #0x54]
    tbz     w1, #5, 10b
    mov     w1, #'*'
    str     w1, [x0, #0x40]
#endif

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
#if BOARD_UART_MINI
    movz    x0, #0x5000
    movk    x0, #0x3f21, lsl #16
11: ldr     w1, [x0, #0x54]
    tbz     w1, #5, 11b
    mov     w1, #'+'
    str     w1, [x0, #0x40]
#endif

    mov     x0, x19
    bl      KernelHandoff
3:  wfi
    b       3b
    .size KernelEntry, .-KernelEntry
