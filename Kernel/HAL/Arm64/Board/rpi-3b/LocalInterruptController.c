/*
 * LocalInterruptController.c — BCM2836/7 每核 CNTV IRQ（psmp-3a）
 *
 * 规格：BCM2836 ARM Quad A7（Gert van Loo）§4；对表 Linux irq-bcm2836.h。
 * LOCAL_TIMER_INT_CONTROL0+4*cpu bit3 = nCNTVIRQ；PENDING0+4*cpu bit3 待处理。
 * 禁写 QEMU virt GIC@0x08000000。
 */
#include "Hal.h"
#include "LocalInterruptController.h"

#define LOCAL_INTERRUPT_CONTROLLER_BASE 0x40000000ULL
#define LOCAL_TIMER_INTERRUPT_CONTROL0  0x040u
#define LOCAL_IRQ_PENDING0              0x060u
#define LOCAL_IRQ_CNTVIRQ               3u
#define LOCAL_INTERRUPT_SPURIOUS        0x3FFu

static volatile UINT32 *LocalRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(LOCAL_INTERRUPT_CONTROLLER_BASE + Offset);
}

void HalLocalInterruptControllerInitializeCpu(void) {
    UINT32 Cpu = HalCpuGetId();
    volatile UINT32 *Control;

    if (Cpu >= 4u) {
        return;
    }
    Control = LocalRegister(LOCAL_TIMER_INTERRUPT_CONTROL0 + 4u * Cpu);
    /* 仅开 CNTV→IRQ；FIQ 位保持 0（否则覆盖 IRQ 使能） */
    *Control = (1u << LOCAL_IRQ_CNTVIRQ);
    __asm__ volatile("dsb sy" ::: "memory");
}

void HalLocalInterruptControllerInitialize(void) {
    HalLocalInterruptControllerInitializeCpu();
}

UINT32 HalLocalInterruptControllerAcknowledge(void) {
    UINT32 Cpu = HalCpuGetId();
    UINT32 Pending;

    if (Cpu >= 4u) {
        return LOCAL_INTERRUPT_SPURIOUS;
    }
    Pending = *LocalRegister(LOCAL_IRQ_PENDING0 + 4u * Cpu);
    if ((Pending & (1u << LOCAL_IRQ_CNTVIRQ)) != 0) {
        return LOCAL_IRQ_CNTVIRQ;
    }
    return LOCAL_INTERRUPT_SPURIOUS;
}

void HalLocalInterruptControllerEndOfInterrupt(UINT32 InterruptId) {
    /* CNTV 无单独 EOI；清 pending 靠 HalTimerAck 重装 cntv_tval */
    (void)InterruptId;
}

int HalLocalInterruptControllerIsTimer(UINT32 InterruptId) {
    return InterruptId == LOCAL_IRQ_CNTVIRQ;
}
