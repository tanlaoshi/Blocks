/*
 * HalTimerStub.c — 非 X64：无 LAPIC 定时器
 *
 * 【初学者】
 * - Hal 占位；Scheduler 走协作式 pause，不开 HalTimerIrqEnable。
 * - 真实现：Hal/X64/HalLapicTimer.c。
 */
#include "HalTimer.h"

int HalTimerInitialize(void) {
    return -1;
}

int HalTimerReady(void) {
    return 0;
}

UINT64 HalTimerTicks(void) {
    return 0;
}

void HalTimerIrqEnable(void) {
}

void HalTimerPollLog(void) {
}

UINT32 HalTimerCurCount(void) {
    return 0;
}
