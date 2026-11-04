/*
 * SmpEntry.S — AP 入口
 *
 * PSCI：context 常=logical；spin-table：x0 未定义。
 * 一律用 MPIDR_EL1 Aff0 作逻辑 CPU（Pi 3B / virt 均 Aff0=0..N-1）。
 *
 * Pi armstub 次核停在 EL2；BSP 已在 KernelEntry 降 EL1。AP 不降就
 * 在 EL2 跑 HalPagingEnableAp（只碰 SCTLR_EL1）→ 永不 gApReady
 * （串口 APs=0 hellos=0，width=32 否证仍死，2026-11-04）。
 */
    .section .text, "ax"
    .global HalApEntry
    .type HalApEntry, %function
HalApEntry:
    mrs     x19, mpidr_el1
    and     x19, x19, #0xff

    /*
     * 极早面包屑：进入口即写 gApEntered[Aff0]=1（MMU off 恒等）。
     * BSP timeout 时可区分「没唤醒」vs「进了入口后死」。
     */
    adrp    x0, gApEntered
    add     x0, x0, :lo12:gApEntered
    mov     w1, #1
    str     w1, [x0, x19, lsl #2]

    mrs     x0, CurrentEL
    cmp     x0, #(2 << 2)
    b.ne    20f

    /* HCR_EL2.RW=1：下层 AArch64（对表 KernelEntry） */
    mov     x0, #(1 << 31)
    msr     hcr_el2, x0
    mov     x0, #3
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr
    mov     x0, #0x33ff
    bic     x0, x0, #(1 << 10)
    msr     cptr_el2, x0
    msr     hstr_el2, xzr

    movz    x0, #0x0830
    movk    x0, #0x30c5, lsl #16
    msr     sctlr_el2, x0
    isb
    movz    x0, #0x0800
    movk    x0, #0x30d0, lsl #16
    msr     sctlr_el1, x0

    /* 先把本核栈写入 SP_EL1，再 eret 进 EL1h */
    adrp    x1, gApStackTop
    add     x1, x1, :lo12:gApStackTop
    ldr     x2, [x1, x19, lsl #3]
    msr     sp_el1, x2
    msr     spsel, #1

    adr     x0, 20f
    msr     elr_el2, x0
    mov     x0, #0x3c5
    msr     spsr_el2, x0
    isb
    eret

20:
    adrp    x1, gApStackTop
    add     x1, x1, :lo12:gApStackTop
    ldr     x2, [x1, x19, lsl #3]
    mov     sp, x2

    mov     x0, x19
    bl      HalApMain
1:  wfi
    b       1b
    .size HalApEntry, .-HalApEntry
