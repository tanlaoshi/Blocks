/*
 * HalTimerStub.c — 非 X64：无 LAPIC tick
 */
#include "HalTimer.h"

int HalTimerInit(void) {
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
