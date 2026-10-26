/*
 * KernelEntry.S — Arm64 入口（保留 x0=DTB）
 *
 * rpi-8：U-Boot 常停 EL2；按 ARM ARM / Linux init_kernel_el 降到 EL1。
 * 曾挂（仅 '*' 无 '+'）：SCTLR RES1 已写但仍死 → 缺 msr SP_EL1
 * （EL1h 用 SP_EL1，EL2 的 sp 是 SP_EL2；见 Arm Community CA53 帖）。
 * 本刀只降 EL；MMU 仍 skip（rpi-9）。
 */
#include "BoardConfig.h"

    .section .text.boot, "ax"
    .global KernelEntry
    .type KernelEntry, %function
KernelEntry:
    mov     x19, x0

#if BOARD_UART_MINI
    /*
     * AUX @0x3F215000：ENABLES|=1、LCR=8bit、CNTL TX+RX，再 '*'。
     * 只开 ENABLES 不够：若 U-Boot 未留 TXEN，CoolTerm 会全哑（屏仍有字）。
     */
    movz    x0, #0x5000
    movk    x0, #0x3f21, lsl #16
    ldr     w1, [x0, #0x04]
    orr     w1, w1, #1
    str     w1, [x0, #0x04]
    mov     w1, #3                 /* LCR 8-bit @+0x4C */
    str     w1, [x0, #0x4c]
    ldr     w1, [x0, #0x60]        /* CNTL */
    orr     w1, w1, #3             /* RXEN|TXEN */
    str     w1, [x0, #0x60]
    mov     w2, #0x10000
10: ldr     w1, [x0, #0x54]
    tbnz    w1, #5, 11f
    subs    w2, w2, #1
    b.ne    10b
11: mov     w1, #'*'
    str     w1, [x0, #0x40]
#endif

    /*
     * EL2 → EL1（CurrentEL==EL2 才降；已在 EL1 则跳过）。
     * 顺序对齐规格 / Linux：HCR.RW → 定时器/CPTR → SCTLR_EL1 RES1 →
     * SP_EL1 → SPSR EL1h|DAIF → ELR → ISB → ERET。
     */
    mrs     x0, CurrentEL
    cmp     x0, #(2 << 2)
    b.ne    20f

    /* HCR_EL2.RW=1：下层 AArch64（Cortex-A53 / nVHE 最小集） */
    mov     x0, #(1 << 31)
    msr     hcr_el2, x0

    /* CNTHCTL_EL2：整寄存器写 0 + EL1PCEN|EL1PCTEN（勿 RMW 留 UNKNOWN） */
    mov     x0, #3
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr

    /* CPTR_EL2：RES1，清 TFP，使 EL1 可碰 SIMD（HalFpu 稍后开 CPACR） */
    mov     x0, #0x33ff
    bic     x0, x0, #(1 << 10)
    msr     cptr_el2, x0
    msr     hstr_el2, xzr

    /*
     * SCTLR_EL2 = INIT_SCTLR_EL2_MMU_OFF（Linux 0x30c50830）：
     * 关掉 EL2 侧 M/C/I，避免与即将 EL1（亦 MMU off）缓存视图打架。
     */
    movz    x0, #0x0830
    movk    x0, #0x30c5, lsl #16
    msr     sctlr_el2, x0
    isb

    /*
     * SCTLR_EL1 = INIT_SCTLR_EL1_MMU_OFF（Linux sysreg.h）：
     * LSMAOE|nTLSMD|EIS|TSCXT|EOS → 0x30d00800；M/C/I=0。
     */
    movz    x0, #0x0800
    movk    x0, #0x30d0, lsl #16
    msr     sctlr_el1, x0

    /* 引导栈写入 SP_EL1（eret 进 EL1h 后 sp≡SP_EL1） */
    adrp    x0, gBootStack
    add     x0, x0, :lo12:gBootStack
    add     x0, x0, #65536
    msr     sp_el1, x0
    msr     spsel, #1

    adr     x0, 20f
    msr     elr_el2, x0
    mov     x0, #0x3c5            /* D|A|I|F | EL1h */
    msr     spsr_el2, x0
    isb
    eret

20:
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
