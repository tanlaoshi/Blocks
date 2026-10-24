/*
 * KernelEntry.S — Arm64 入口（保留 x0=DTB）
 *
 * 真机手测（2026-10-24）：带 SCTLR RES1 的 EL2→EL1 仍可能只打出 '*'、无 '+'
 * （死在 eret）。能出 Boot: Mod 的那次是停在 EL2 + 后改 skip MMU。
 * 故暂不降级；异常级专刀另做。入口信标保留。
 */
#include "BoardConfig.h"

    .section .text.boot, "ax"
    .global KernelEntry
    .type KernelEntry, %function
KernelEntry:
    mov     x19, x0

#if BOARD_UART_MINI
    /* AUX @0x3F215000；ENABLES|=1，有限等待后 '*' */
    movz    x0, #0x5000
    movk    x0, #0x3f21, lsl #16
    ldr     w1, [x0, #0x04]
    orr     w1, w1, #1
    str     w1, [x0, #0x04]
    mov     w2, #0x10000
10: ldr     w1, [x0, #0x54]
    tbnz    w1, #5, 11f
    subs    w2, w2, #1
    b.ne    10b
11: mov     w1, #'*'
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
    mov     w2, #0x10000
12: ldr     w1, [x0, #0x54]
    tbnz    w1, #5, 13f
    subs    w2, w2, #1
    b.ne    12b
13: mov     w1, #'+'
    str     w1, [x0, #0x40]
#endif

    mov     x0, x19
    bl      KernelHandoff
3:  wfi
    b       3b
    .size KernelEntry, .-KernelEntry
