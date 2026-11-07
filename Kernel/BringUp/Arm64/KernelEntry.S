/*
 * KernelEntry.S — Arm64 入口（保留 x0=DTB）
 *
 * rpi-8：U-Boot 常停 EL2；按 ARM ARM / Linux init_kernel_el 降到 EL1。
 * Pixel 8：ABL 已在 EL1；BOARD_LINUX_IMAGE_HEADER=1 时文首为 Linux Image 头。
 */
#include "BoardConfig.h"

#ifndef BOARD_LINUX_IMAGE_HEADER
#define BOARD_LINUX_IMAGE_HEADER 0
#endif
#ifndef BOARD_UART_SAMSUNG
#define BOARD_UART_SAMSUNG 0
#endif
#ifndef BOARD_UART_MINI
#define BOARD_UART_MINI 0
#endif

    .section .text.boot, "ax"
    .global KernelEntry
    .type KernelEntry, %function
KernelEntry:
#if BOARD_LINUX_IMAGE_HEADER
    /*
     * Linux aarch64 Image header（Documentation/arch/arm64/booting.rst）。
     * code0 = 跳过本头；text_offset 板可覆；flags bit3=可任意放置。
     */
#ifndef BOARD_LINUX_IMAGE_TEXT_OFFSET
#define BOARD_LINUX_IMAGE_TEXT_OFFSET 0x80000
#endif
    b       5f
    .long   0
    .quad   BOARD_LINUX_IMAGE_TEXT_OFFSET
    .quad   0x200000
    .quad   0xa
    .quad   0
    .quad   0
    .quad   0
    .long   0x644d5241
    .long   0
5:
#endif
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

#if BOARD_UART_SAMSUNG
    /* Exynos UART @BOARD_UART_BASE：UTRSTAT TXE 后写 UTXH='*'（ABL 已配钟） */
    movz    x0, #0x0000
    movk    x0, #0x1087, lsl #16
    mov     w2, #0x10000
14: ldr     w1, [x0, #0x10]
    tbz     w1, #2, 15f
    b       16f
15: subs    w2, w2, #1
    b.ne    14b
16: mov     w1, #'*'
    str     w1, [x0, #0x20]
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

#if BOARD_UART_SAMSUNG
    movz    x0, #0x0000
    movk    x0, #0x1087, lsl #16
    mov     w2, #0x10000
17: ldr     w1, [x0, #0x10]
    tbz     w1, #2, 18f
    b       19f
18: subs    w2, w2, #1
    b.ne    17b
19: mov     w1, #'+'
    str     w1, [x0, #0x20]
#endif

    mov     x0, x19
    bl      KernelHandoff
3:  wfi
    b       3b
    .size KernelEntry, .-KernelEntry
