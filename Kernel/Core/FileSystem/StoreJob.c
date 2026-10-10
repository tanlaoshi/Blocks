/*
 * StoreJob.c — 装包互斥 + 单槽排队（薄，无 WorkerTask）
 *
 * 【初学者】
 * - 分层：Core/FileSystem；实际拷贝在 Store.c
 * - 对外：StoreJobInstall / StoreJobStep / StoreJobIsBusy
 * - 不做：后台线程；并发多包队列
 */
#include "StoreJob.h"
#include "HalSerial.h"
#include "SerialConfig.h"

static int gBusy;
static char gPendingId[STORE_ID_MAX];
static int gHavePending;
static int gLastError;

/*
 * CopyIdentifier — 拷贝包 id 到固定缓冲
 *
 * 做什么：截断到 STORE_ID_MAX-1。
 * 谁调用：仅 StoreJobInstall。
 * 返回：void
 */
static void CopyIdentifier(char *Destination, const char *Source) {
    int Index = 0;

    if (Destination == 0) {
        return;
    }
    if (Source == 0) {
        Destination[0] = 0;
        return;
    }
    while (Source[Index] && Index < STORE_ID_MAX - 1) {
        Destination[Index] = Source[Index];
        Index++;
    }
    Destination[Index] = 0;
}

/*
 * StoreJobIsBusy — 是否有进行中的装包
 *
 * 做什么：读 gBusy。
 * 谁调用：Shell `install` / StoreUi 禁用重复点。
 * 返回：1 忙；0 空闲
 */
int StoreJobIsBusy(void) {
    return gBusy;
}

/*
 * StoreJobBegin — 占互斥（单槽）
 *
 * 做什么：gBusy 置 1；已忙则 STORE_BUSY。
 * 谁调用：StoreJobInstall。
 * 返回：STORE_OK / STORE_BUSY
 */
int StoreJobBegin(void) {
    if (gBusy) {
        HalSerialWriteChannel(SLOG_FS, "StoreJob: busy\n");
        return STORE_BUSY;
    }
    gBusy = 1;
    return STORE_OK;
}

/*
 * StoreJobEnd — 释放互斥并清 pending
 *
 * 做什么：复位 gBusy / gHavePending。
 * 谁调用：错误路径（正常由 StoreJobStep 收尾）。
 * 返回：void
 */
void StoreJobEnd(void) {
    gBusy = 0;
    gHavePending = 0;
    gPendingId[0] = 0;
}

/*
 * StoreJobInstall — 排队安装某 catalog id
 *
 * 做什么：Begin → 记 gPendingId → 等 StoreJobStep 执行 StoreInstall。
 * 谁调用：Shell `install`；StoreUi 按钮。
 * 前后文：后 — StoreJobStep → StoreInstall。
 * 返回：STORE_OK / STORE_BUSY / STORE_INVAL
 */
int StoreJobInstall(const char *Id) {
    if (Id == 0 || Id[0] == 0) {
        return STORE_INVAL;
    }
    if (StoreJobBegin() != STORE_OK) {
        return STORE_BUSY;
    }
    CopyIdentifier(gPendingId, Id);
    gHavePending = 1;
    gLastError = STORE_OK;
    HalSerialWriteChannel(SLOG_FS, "StoreJob: queued\n");
    return STORE_OK;
}

/*
 * StoreJobStep — 主循环调一次：若有 pending 则 StoreInstall
 *
 * 做什么：调 StoreInstall；清 busy；记 gLastError。
 * 谁调用：Console 主循环 / Gui 帧 tick。
 * 返回：1 执行了一步；0 无 pending
 */
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

/*
 * StoreJobLastError — 最近一次 Step 的结果码
 *
 * 做什么：返回 gLastError。
 * 谁调用：Shell 打印 install 结果。
 * 返回：STORE_* 常量
 */
int StoreJobLastError(void) {
    return gLastError;
}
