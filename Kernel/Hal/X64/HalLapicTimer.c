/*
 * HalLapicTimer.c — X64 LAPIC 周期定时器（K18）
 *
 * 【初学者】
 * MSR 开 APIC → 屏蔽杂散 LVT → SVR enable → LVT Timer 周期模式。
 * 向量 0x30，避开 8259 的 0x20–0x2F。
 */
#include "HalTimer.h"
#include "HalCpu.h"
#include "HalSerial.h"
#include "SerialConfig.h"

#define LAPIC_DEFAULT   0xFEE00000ull
#define LAPIC_MSR       0x1Bu
#define TIMER_VEC       0x30u
#define LAPIC_ID        0x020u
#define LAPIC_TPR       0x080u
#define LAPIC_EOI       0x0B0u
#define LAPIC_SVR       0x0F0u
#define LAPIC_LVT_TIMER 0x320u
#define LAPIC_LVT_LINT0 0x350u
#define LAPIC_LVT_LINT1 0x360u
#define LAPIC_LVT_ERR   0x370u
#define LAPIC_LVT_PERF  0x340u
#define LAPIC_LVT_THERM 0x330u
#define LAPIC_INIT_CNT  0x380u
#define LAPIC_CUR_CNT   0x390u
#define LAPIC_DIVIDE    0x3E0u

#define SVR_ENABLE      (1u << 8)
#define LVT_PERIODIC    (1u << 17)
#define LVT_MASKED      (1u << 16)
#define MSR_APIC_EN     (1ull << 11)
#define MSR_X2APIC      (1ull << 10)

#define TIMER_INIT_CNT  1000000u

extern void HalCpuIsrTimer(void);

static volatile UINT32 *gLapic;
static volatile UINT64 gTicks;
static int gReady;

static UINT64 RdMsr(UINT32 Index) {
    UINT32 Lo, Hi;
    __asm__ volatile("rdmsr" : "=a"(Lo), "=d"(Hi) : "c"(Index));
    return ((UINT64)Hi << 32) | Lo;
}

static void WrMsr(UINT32 Index, UINT64 Val) {
    UINT32 Lo = (UINT32)Val;
    UINT32 Hi = (UINT32)(Val >> 32);
    __asm__ volatile("wrmsr" : : "c"(Index), "a"(Lo), "d"(Hi));
}

static void LapicW(UINT32 Off, UINT32 Val) {
    gLapic[Off / 4u] = Val;
    __asm__ volatile("mfence" ::: "memory");
    (void)gLapic[LAPIC_ID / 4u];
}

static UINT32 LapicR(UINT32 Off) {
    return gLapic[Off / 4u];
}

static void TimerArm(void) {
    LapicW(LAPIC_DIVIDE, 0x3u);
    LapicW(LAPIC_LVT_TIMER, LVT_PERIODIC | TIMER_VEC);
    LapicW(LAPIC_INIT_CNT, TIMER_INIT_CNT);
}

void HalTimerIrq(void) {
    gTicks++;
    LapicW(LAPIC_EOI, 0);
    TimerArm();
}

int HalTimerInitialize(void) {
    UINT64 Msr;
    UINT64 Base;
    UINT64 Start;
    UINT32 Spin;

    gReady = 0;
    gTicks = 0;

    Msr = RdMsr(LAPIC_MSR);
    if (Msr & MSR_X2APIC) {
        Msr &= ~MSR_X2APIC;
        WrMsr(LAPIC_MSR, Msr);
        Msr = RdMsr(LAPIC_MSR);
    }
    if ((Msr & MSR_APIC_EN) == 0) {
        Msr |= MSR_APIC_EN;
        if ((Msr & 0x000FFFFFFFFFF000ull) == 0) {
            Msr |= LAPIC_DEFAULT;
        }
        WrMsr(LAPIC_MSR, Msr);
        Msr = RdMsr(LAPIC_MSR);
    }
    Base = Msr & 0x000FFFFFFFFFF000ull;
    if (Base == 0) {
        Base = LAPIC_DEFAULT;
    }
    gLapic = (volatile UINT32 *)(UINTN)Base;

    HalCpuIdtSet(TIMER_VEC, (void *)HalCpuIsrTimer);

    __asm__ volatile("mov %0, %%cr8" : : "r"((UINT64)0) : "memory");
    LapicW(LAPIC_TPR, 0);
    LapicW(LAPIC_SVR, SVR_ENABLE | 0xFFu);
    LapicW(LAPIC_EOI, 0);
    LapicW(LAPIC_LVT_LINT0, LVT_MASKED);
    LapicW(LAPIC_LVT_LINT1, LVT_MASKED);
    LapicW(LAPIC_LVT_ERR, LVT_MASKED);
    LapicW(LAPIC_LVT_PERF, LVT_MASKED);
    LapicW(LAPIC_LVT_THERM, LVT_MASKED);
    LapicW(LAPIC_LVT_TIMER, LVT_MASKED | TIMER_VEC);
    TimerArm();

    __asm__ volatile("sti" ::: "memory");
    Start = gTicks;
    for (Spin = 0; Spin < 80000000u; Spin++) {
        if (gTicks >= Start + 2u) {
            break;
        }
        __asm__ volatile("pause");
    }
    if (gTicks < Start + 2u) {
        HalSerialWriteChannel(SLOG_MISC, "Sched: WARN no lapic irq\n");
        LapicW(LAPIC_LVT_TIMER, LVT_MASKED | TIMER_VEC);
        __asm__ volatile("cli" ::: "memory");
        gReady = 0;
        return -1;
    }

    TimerArm();
    gReady = 1;
    return 0;
}

int HalTimerReady(void) {
    return gReady;
}

UINT64 HalTimerTicks(void) {
    return gTicks;
}

void HalTimerIrqEnable(void) {
    __asm__ volatile("sti" ::: "memory");
}

UINT32 HalTimerCurCount(void) {
    if (gLapic == 0) {
        return 0;
    }
    return LapicR(LAPIC_CUR_CNT);
}

/* K18 曾打 Sched: tick；会打断 Shell 输入行，默认关闭。节拍本身仍在跑。 */
void HalTimerPollLog(void) {
}
