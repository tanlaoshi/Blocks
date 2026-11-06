/*
 * ReceiveBatch.c — 收块批写缓冲（有线攒批；Wi‑Fi 每块刷）
 *
 * 'A' 在 Receive 侧于本文件返回后立刻发出；满批只 flush 已 A 过的旧批。
 */
#include "Ksend/Private.h"
#include "HalDevices.h"
#include "LwIp.h"
#include "Scheduler.h"

#define KSEND_BATCH_LIMIT_BYTES KSEND_IO_CHUNK_BYTES

static UINT32 sBatchFirstIndex;
static UINT32 sBatchBlocks;
static UINT32 sBatchBytes;

void KsendReceiveBatchReset(void) {
    sBatchFirstIndex = 0;
    sBatchBlocks = 0;
    sBatchBytes = 0;
}

static UINT32 BatchLimitBytes(void) {
    if (HalNetWifiShouldOwnTransmit()) {
        return KSEND_BLOCK_BYTES;
    }
    return KSEND_BATCH_LIMIT_BYTES;
}

static void CopyBlock(UINT8 *Destination, const UINT8 *Source, UINT32 Length) {
    UINT32 Index;

    for (Index = 0; Index < Length; Index++) {
        Destination[Index] = Source[Index];
    }
}

int KsendReceiveBatchFlush(KSEND_RUN *Run) {
    UINT32 Offset;
    UINT32 Done;
    int Error;

    if (sBatchBytes == 0u) {
        return 1;
    }
    Offset = sBatchFirstIndex * KSEND_BLOCK_BYTES;
    Done = 0;
    while (Done < sBatchBytes) {
        UINT32 Take = sBatchBytes - Done;

        if (Take > KSEND_BLOCK_BYTES) {
            Take = KSEND_BLOCK_BYTES;
        }
        Error = KsendFileWriteAt(Run->TargetPath, Offset + Done,
                                 KsendFileBuffer() + Done, Take);
        if (Error != FAT_OK) {
            Run->Error = Error;
            Run->FailBlock = sBatchFirstIndex + sBatchBlocks - 1u;
            KsendReceiveBatchReset();
            return 0;
        }
        Done += Take;
        if (LwIpActive()) {
            LwIpService();
            SchedulerIoBreath();
            if (HalNetWifiShouldOwnTransmit()) {
                LwIpService();
                SchedulerIoBreath();
            }
        }
    }
    KsendReceiveBatchReset();
    return 1;
}

int KsendReceiveBatchAccept(KSEND_RUN *Run, UINT32 Index, UINT32 Length,
                            const UINT8 *Data) {
    UINT32 At;
    UINT32 Limit = BatchLimitBytes();

    if (!Run || !Data || Length == 0u || Length > KSEND_BLOCK_BYTES) {
        return 0;
    }
    if (sBatchBlocks > 0u &&
        (Index != sBatchFirstIndex + sBatchBlocks ||
         Length != KSEND_BLOCK_BYTES)) {
        if (!KsendReceiveBatchFlush(Run)) {
            return 0;
        }
    }
    if (sBatchBlocks > 0u && (sBatchBytes + Length) > Limit) {
        if (!KsendReceiveBatchFlush(Run)) {
            return 0;
        }
    }
    if (sBatchBlocks == 0u) {
        sBatchFirstIndex = Index;
    }
    At = (Index - sBatchFirstIndex) * KSEND_BLOCK_BYTES;
    CopyBlock(KsendFileBuffer() + At, Data, Length);
    sBatchBlocks++;
    sBatchBytes = At + Length;
    if (Length != KSEND_BLOCK_BYTES || sBatchBytes >= Limit) {
        if (!KsendReceiveBatchFlush(Run)) {
            return 0;
        }
    }
    return 1;
}
