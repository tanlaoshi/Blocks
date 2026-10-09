/*
 * Scheduler.c — K8 壳 + K18 LAPIC 节拍
 *
 * 【初学者】
 * 开 HalTimer 后 sti；Yield 里 hlt 睡到下一拍（不再刷 tick 日志）。
 */
#include "Scheduler.h"
#include "HalSerial.h"
#include "HalTimer.h"
#include "ToySerialConfig.h"

static int gSchedReady;

int SchedulerInitialize(void) {
    gSchedReady = 0;
#if defined(__x86_64__) || defined(_M_X64)
    if (HalTimerInit() == 0 && HalTimerReady()) {
        HalTimerIrqEnable();
        HalSerialWriteChannel(TOY_SLOG_MISC, "Scheduler: timer ok\n");
        gSchedReady = 1;
        return 0;
    }
    HalSerialWriteChannel(TOY_SLOG_MISC, "Scheduler: timer skip (coop)\n");
#endif
    gSchedReady = 1;
    return 0;
}

void SchedulerYield(void) {
    if (!gSchedReady) {
        return;
    }
#if defined(__x86_64__) || defined(_M_X64)
    if (HalTimerReady()) {
        HalTimerPollLog();
        /* STI 延迟一拍：中间必须再有一条指令，否则 hlt 时 IF 仍 0 */
        __asm__ volatile("sti; nop; hlt" ::: "memory");
    } else {
        __asm__ volatile("pause");
    }
#else
    (void)0;
#endif
}
