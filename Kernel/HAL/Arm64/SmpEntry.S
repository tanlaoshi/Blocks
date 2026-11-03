/*
 * SmpEntry.S — AP 入口
 *
 * PSCI：context 常=logical；spin-table：x0 未定义。
 * 一律用 MPIDR_EL1 Aff0 作逻辑 CPU（Pi 3B / virt 均 Aff0=0..N-1）。
 */
    .section .text, "ax"
    .global HalApEntry
    .type HalApEntry, %function
HalApEntry:
    mrs     x19, mpidr_el1
    and     x19, x19, #0xff

    adrp    x1, gApStackTop
    add     x1, x1, :lo12:gApStackTop
    ldr     x2, [x1, x19, lsl #3]
    mov     sp, x2

    mov     x0, x19
    bl      HalApMain
1:  wfi
    b       1b
    .size HalApEntry, .-HalApEntry
