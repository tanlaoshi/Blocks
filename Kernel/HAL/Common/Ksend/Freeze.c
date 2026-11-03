/*
 * Freeze.c — ksend 热更冻结：串口一行 + 可选 Gui 大窗（经 Ops）
 */
#include "Ksend/Private.h"
#include "Hal/KsendFreeze.h"
#include "HalConsole.h"

static const KSEND_FREEZE_OPS *gKsendFreezeOperations;
static int gKsendFreezeActive;

void KsendFreezeOpsRegister(const KSEND_FREEZE_OPS *Operations) {
    gKsendFreezeOperations = Operations;
}

void KsendFreezeBegin(void) {
    if (gKsendFreezeActive) {
        return;
    }
    gKsendFreezeActive = 1;
    HalConsoleWriteSerial("热更中\n");
    if (gKsendFreezeOperations != 0 && gKsendFreezeOperations->Begin != 0) {
        gKsendFreezeOperations->Begin();
    }
}

void KsendFreezeEnd(void) {
    if (gKsendFreezeActive == 0) {
        return;
    }
    if (gKsendFreezeOperations != 0 && gKsendFreezeOperations->End != 0) {
        gKsendFreezeOperations->End();
    }
    gKsendFreezeActive = 0;
}
