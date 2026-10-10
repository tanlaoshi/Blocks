/*
 * StoreJob.c — K48：装包互斥 + 单槽排队（薄，无 WorkerTask）
 *
 * 【初学者】
 * Install 入队即返回；Console/Gui 轮询 StoreJobStep 真正拷文件。
 * Busy 时再 Install → STORE_BUSY（Shell/窗互斥）。
 */
#include "StoreJob.h"
#include "HalSerial.h"
#include "SerialConfig.h"

static int gBusy;
static char gPendingId[STORE_ID_MAX];
static int gHavePending;
static int gLastError;

static void CopyId(char *Dst, const char *Src) {
    int i = 0;

    if (Dst == 0) {
        return;
    }
    if (Src == 0) {
        Dst[0] = 0;
        return;
    }
    while (Src[i] && i < STORE_ID_MAX - 1) {
        Dst[i] = Src[i];
        i++;
    }
    Dst[i] = 0;
}

int StoreJobIsBusy(void) {
    return gBusy;
}

int StoreJobBegin(void) {
    if (gBusy) {
        HalSerialWriteChannel(SLOG_FS, "StoreJob: busy\n");
        return STORE_BUSY;
    }
    gBusy = 1;
    return STORE_OK;
}

void StoreJobEnd(void) {
    gBusy = 0;
    gHavePending = 0;
    gPendingId[0] = 0;
}

int StoreJobInstall(const char *Id) {
    if (Id == 0 || Id[0] == 0) {
        return STORE_INVAL;
    }
    if (StoreJobBegin() != STORE_OK) {
        return STORE_BUSY;
    }
    CopyId(gPendingId, Id);
    gHavePending = 1;
    gLastError = STORE_OK;
    HalSerialWriteChannel(SLOG_FS, "StoreJob: queued\n");
    return STORE_OK;
}

int StoreJobStep(void) {
    int Error;

    if (!gHavePending) {
        return 0;
    }
    Error = StoreInstall(gPendingId);
    gLastError = Error;
    gHavePending = 0;
    gPendingId[0] = 0;
    gBusy = 0;
    if (Error == STORE_OK) {
        HalSerialWriteChannel(SLOG_FS, "StoreJob: done\n");
    } else {
        HalSerialWriteChannel(SLOG_FS, "StoreJob: fail\n");
    }
    return 1;
}

int StoreJobLastError(void) {
    return gLastError;
}
