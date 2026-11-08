/*
 * HalTimer.c — Arm64 CNTV 定时器 + IRQ 派发（virt=GIC；Pi=local intc / psmp-3a）
 */
#include "Hal.h"
#include "HalConsole.h"
#include "Scheduler.h"
#include "BoardConfig.h"
#if !BOARD_IS_VIRT
#include "LocalInterruptController.h"
#endif

static UINT64 gCntLast;
static UINT64 gCntFreq;
static UINT64 gCntPeriod;
static UINT32 gTimerMs = 10;
static int gTimerReady;
static int gTimerIrq;

extern void HalExceptionVectorsInstall(void);
#if BOARD_IS_VIRT
extern void HalGicInitialize(void);
extern void HalGicInitializeCpu(void);
extern UINT32 HalGicAck(void);
extern void HalGicEoi(UINT32 IntId);
extern int HalGicIsTimer(UINT32 IntId);
#endif

void HalCpuHalt(void) {
    if (gTimerIrq) {
        HalIrqEnable();
        __asm__ volatile("wfi" ::: "memory");
        HalIrqDisable();
        return;
    }
    HalTimerPoll();
    HalCpuRelax();
}

void HalIrqEoi(UINT32 Vector) {
#if BOARD_IS_VIRT
    HalGicEoi(Vector);
#else
    HalLocalInterruptControllerEndOfInterrupt(Vector);
#endif
}

static void ArmTimerArm(void) {
    __asm__ volatile("msr cntv_tval_el0, %0" ::"r"(gCntPeriod) : "memory");
    __asm__ volatile("msr cntv_ctl_el0, %0" ::"r"(1ULL) : "memory");
    __asm__ volatile("isb" ::: "memory");
}

void HalTimerInitialize(void) {
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(gCntFreq));
    if (gCntFreq == 0) {
        gCntFreq = 62500000ULL;
    }
    gCntPeriod = gCntFreq / 100;
    if (gCntPeriod == 0) {
        gCntPeriod = 1;
    }
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(gCntLast));
    gTimerReady = 1;
}

void HalTimerSetInterval(UINT32 Milliseconds) {
    if (Milliseconds == 0) {
        Milliseconds = 10;
    }
    gTimerMs = Milliseconds;
    if (gCntFreq == 0) {
        return;
    }
    gCntPeriod = (gCntFreq * (UINT64)Milliseconds) / 1000ULL;
    if (gCntPeriod == 0) {
        gCntPeriod = 1;
    }
}

void HalTimerAck(void) {
    ArmTimerArm();
}

void HalTimerStart(void) {
    if (!gTimerReady) {
        HalTimerInitialize();
    }
    HalExceptionVectorsInstall();
#if BOARD_IS_VIRT
    HalGicInitialize();
    HalTimerSetInterval(gTimerMs);
    ArmTimerArm();
    gTimerIrq = 1;
    HalSerialWrite("timer: Arm64 CNTV+GIC irq\n");
#else
    /* psmp-3a：BCM2836/7 local intc @0x40000000；禁 virt GIC */
    HalLocalInterruptControllerInitialize();
    HalTimerSetInterval(gTimerMs);
    ArmTimerArm();
    gTimerIrq = 1;
    /*
     * SchedulerStart 在 Announce/ShellOwn 之后：BootMark 已不占 UART。
     * 用 ConsoleWriteSerial 才能在 CoolTerm 看见验收行。
     */
    HalConsoleWriteSerial("Boot: Pi CNTV local IRQ\n");
#endif
}

UINT32 HalTicksPerSec(void) {
    return 1000;
}

UINT32 HalMilliseconds(void) {
    UINT64 Now;
    UINT64 Freq;

    if (gCntFreq == 0) {
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(gCntFreq));
        if (gCntFreq == 0) {
            gCntFreq = 62500000ULL;
        }
    }
    Freq = gCntFreq;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(Now));
    return (UINT32)((Now * 1000ULL) / Freq);
}

void HalDelayMilliseconds(UINT32 Milliseconds) {
    UINT32 Start = HalMilliseconds();

    while ((UINT32)(HalMilliseconds() - Start) < Milliseconds) {
        HalCpuRelax();
    }
}

void HalTimerStartAp(void) {
    if (!gTimerReady) {
        HalTimerInitialize();
    }
    HalExceptionVectorsInstall();
#if BOARD_IS_VIRT
    HalGicInitializeCpu();
#else
    HalLocalInterruptControllerInitializeCpu();
#endif
    ArmTimerArm();
    /*
     * 勿在此置 gTimerIrq：Smp 模块拉 AP 早于 Gui Desktop Ready。
     * 全局 UsesIrq=1 会让 Ready 走 Hold，且 Worker 早露标被跳过；
     * Secondary/DHCP continue 一卡 → 进桌面鼠死（Pi 2026-11-08）。
     * IRQ 策略位只由 BSP HalTimerStart（SchedulerStart）置位。
     */
}

void HalTimerPoll(void) {
    UINT64 Now;
    UINT64 Delta;

    if (!gTimerReady || gTimerIrq) {
        return;
    }
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(Now));
    Delta = Now - gCntLast;
    while (Delta >= gCntPeriod) {
        HalCpuIncrementTicks();
        gCntLast += gCntPeriod;
        Delta -= gCntPeriod;
    }
}

int HalTimerUsesIrq(void) {
    return gTimerIrq ? 1 : 0;
}

UINT64 HalExceptionIrqDispatch(HAL_INTERRUPT_FRAME *Frame) {
    UINT32 Id;
    UINT64 Ret = 0;

#if BOARD_IS_VIRT
    Id = HalGicAck();
    if (HalGicIsTimer(Id)) {
        HalCpuIncrementTicks();
        HalTimerAck();
        HalGicEoi(Id);
        if (SchedulerIsOnline() && Frame) {
            Ret = SchedulerOnTimer(Frame);
        }
        return Ret;
    }
    HalGicEoi(Id);
#else
    Id = HalLocalInterruptControllerAcknowledge();
    if (HalLocalInterruptControllerIsTimer(Id)) {
        HalCpuIncrementTicks();
        HalTimerAck();
        HalLocalInterruptControllerEndOfInterrupt(Id);
        if (SchedulerIsOnline() && Frame) {
            Ret = SchedulerOnTimer(Frame);
        }
        return Ret;
    }
    HalLocalInterruptControllerEndOfInterrupt(Id);
#endif
    return 0;
}
