/*
 * ReceiveBatch.c — 收块批写缓冲（有线/Wi‑Fi）
 *
 * 契约：Accept 成功 ⇒ 本块已在 TargetPath 落盘（与协议 'A'=已写卡一致）。
 * 有线暂每块刷：先 A 后攒批会在 win>1 时假进度，flush 失败已 ACK 难恢复
 * （NUC 2026-11-08：~16 块 err=-10 / RESTORED）。大批+延迟 A 见吞吐刀。
 */
#include "Ksend/Private.h"
#include "HalDevices.h"
#include "LwIp.h"
#include "Scheduler.h"

static UINT32 sBatchFirstIndex;
static UINT32 sBatchBlocks;
static UINT32 sBatchBytes;

void KsendReceiveBatchReset(void) {
    sBatchFirstIndex = 0;
    sBatchBlocks = 0;
    sBatchBytes = 0;
}

static void CopyBlock(UINT8 *Destination, const UINT8 *Source, UINT32 Length) {
    UINT32 Index;

    for (Index = 0; Index < Length; Index++) {
        Destination[Index] = Source[Index];
    }
}

static void BreathWhileWired(void) {
    if (!LwIpActive()) {
        return;
    }
    LwIpService();
    SchedulerIoBreath();
    if (HalNetWifiShouldOwnTransmit()) {
        LwIpService();
        SchedulerIoBreath();
    }
}

int KsendReceiveBatchFlush(KSEND_RUN *Run) {
    UINT32 Offset;
    UINT32 Done;
    int Error;

    if (sBatchBytes == 0u) {
        return 1;
    }
    if (sBatchFirstIndex >= KSEND_MAX_BLOCKS) {
        Run->Error = FAT_ERR_INVAL;
        Run->FailBlock = sBatchFirstIndex;
        KsendReceiveBatchReset();
        return 0;
    }
    Offset = sBatchFirstIndex * KSEND_BLOCK_BYTES;
    if (Offset > KSEND_MAX_BYTES || sBatchBytes > KSEND_MAX_BYTES ||
        Offset + sBatchBytes > KSEND_MAX_BYTES) {
        Run->Error = FAT_ERR_FILE_TOO_BIG;
        Run->FailBlock = sBatchFirstIndex;
        KsendReceiveBatchReset();
        return 0;
    }
    Done = 0;
    while (Done < sBatchBytes) {
        UINT32 Take = sBatchBytes - Done;
        UINT32 BlockIndex;

        if (Take > KSEND_BLOCK_BYTES) {
            Take = KSEND_BLOCK_BYTES;
        }
        BlockIndex = sBatchFirstIndex + (Done / KSEND_BLOCK_BYTES);
        Error = KsendFileWriteAt(Run->TargetPath, Offset + Done,
                                 KsendFileBuffer() + Done, Take);
        if (Error != FAT_OK) {
            Run->Error = Error;
            Run->FailBlock = BlockIndex;
            KsendReceiveBatchReset();
            return 0;
        }
        Done += Take;
        BreathWhileWired();
    }
    KsendReceiveBatchReset();
    return 1;
}

int KsendReceiveBatchAccept(KSEND_RUN *Run, UINT32 Index, UINT32 Length,
                            const UINT8 *Data) {
    UINT32 At;

    if (!Run || !Data || Length == 0u || Length > KSEND_BLOCK_BYTES) {
        if (Run != 0) {
            Run->Error = FAT_ERR_INVAL;
            Run->FailBlock = Index;
        }
        return 0;
    }
    if (Index >= KSEND_MAX_BLOCKS) {
        Run->Error = FAT_ERR_INVAL;
        Run->FailBlock = Index;
        return 0;
    }
    /*
     * 有线批写刀：每块独立落盘。Wi‑Fi 亦每块刷（与旧行为同）。
     * 不在 RAM 里跨块攒批，避免先 A 后炸。
     */
    if (sBatchBlocks > 0u) {
        if (!KsendReceiveBatchFlush(Run)) {
            return 0;
        }
    }
    sBatchFirstIndex = Index;
    At = 0;
    if (Length > KSEND_IO_CHUNK_BYTES) {
        Run->Error = FAT_ERR_INVAL;
        Run->FailBlock = Index;
        return 0;
    }
    CopyBlock(KsendFileBuffer() + At, Data, Length);
    sBatchBlocks = 1;
    sBatchBytes = Length;
    if (!KsendReceiveBatchFlush(Run)) {
        return 0;
    }
    return 1;
}
