/*
 * ReceiveBatch.c — 收块批写（有线按 win 攒批；落盘后再齐 ACK）
 *
 * 契约：Flush 成功后 Ack 表有效，调用方发等量 'A'。
 * 上限 = READY win=（有线 8；串口/Wi‑Fi 1）。整批一次 WriteAt。
 */
#include "Ksend/Private.h"
#include "Ksend/TransportPrivate.h"
#include "Hal.h"
#include "HalPlatform.h"
#include "LwIp.h"
#include "Scheduler.h"

static UINT32 sBatchFirstIndex;
static UINT32 sBatchBlocks;
static UINT32 sBatchBytes;
static UINT32 sBatchCrc[KSEND_WINDOW_MAX];
static UINT32 sBatchLength[KSEND_WINDOW_MAX];

static UINT32 sAckCount;
static UINT32 sAckIndex[KSEND_WINDOW_MAX];
static UINT32 sAckLength[KSEND_WINDOW_MAX];
static UINT32 sAckCrc[KSEND_WINDOW_MAX];

void KsendReceiveBatchReset(void) {
    sBatchFirstIndex = 0;
    sBatchBlocks = 0;
    sBatchBytes = 0;
    sAckCount = 0;
}

UINT32 KsendReceiveBatchAckCount(void) {
    return sAckCount;
}

int KsendReceiveBatchAckAt(UINT32 Slot, UINT32 *OutIndex, UINT32 *OutLength,
                           UINT32 *OutCrc) {
    if (Slot >= sAckCount || OutIndex == 0 || OutLength == 0 || OutCrc == 0) {
        return 0;
    }
    *OutIndex = sAckIndex[Slot];
    *OutLength = sAckLength[Slot];
    *OutCrc = sAckCrc[Slot];
    return 1;
}

UINT32 KsendReceiveBatchPendingBlocks(void) {
    return sBatchBlocks;
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
    /*
     * 落盘长圈须泵网（Pi win 攒批写 SD 曾饿死 TCP）。
     * Pi VirtDesktop：无 InputTask → IoBreath 代泵 HID + Ctrl-C。
     * X64：禁 IoBreath（InputTask 主人；双泵饿 xHCI/网 —— NUC 2026-11-08
     * 首窗 8 块后 hotsend exit2，像复制了 Pi 大文件问题）。
     */
    if (!HalCpuIsHypervisor() && HalPlatformUseVirtDesktop()) {
        SchedulerIoBreath();
    } else {
        SchedulerSetNeedResched();
        (void)SchedulerCondResched();
    }
    (void)TransportPollUserAbort();
}

static void PublishAcksFromBatch(void) {
    UINT32 Slot;

    sAckCount = sBatchBlocks;
    for (Slot = 0; Slot < sBatchBlocks && Slot < KSEND_WINDOW_MAX; Slot++) {
        sAckIndex[Slot] = sBatchFirstIndex + Slot;
        sAckLength[Slot] = sBatchLength[Slot];
        sAckCrc[Slot] = sBatchCrc[Slot];
    }
}

static UINT32 BatchLimitBlocks(void) {
    UINT32 Window = KsendReadyWindowBlocks();

    if (Window == 0u || Window > KSEND_WINDOW_MAX) {
        Window = 1u;
    }
    return Window;
}

int KsendReceiveBatchFlush(KSEND_RUN *Run) {
    UINT32 Offset;
    int Error;

    sAckCount = 0;
    if (sBatchBytes == 0u) {
        return 1;
    }
    if (sBatchFirstIndex >= KSEND_MAX_BLOCKS || sBatchBlocks == 0u ||
        sBatchBlocks > KSEND_WINDOW_MAX) {
        Run->Error = FAT_ERR_INVAL;
        Run->FailBlock = sBatchFirstIndex;
        sBatchFirstIndex = 0;
        sBatchBlocks = 0;
        sBatchBytes = 0;
        return 0;
    }
    Offset = sBatchFirstIndex * KSEND_BLOCK_BYTES;
    if (Offset > KSEND_MAX_BYTES || sBatchBytes > KSEND_MAX_BYTES ||
        Offset + sBatchBytes > KSEND_MAX_BYTES) {
        Run->Error = FAT_ERR_FILE_TOO_BIG;
        Run->FailBlock = sBatchFirstIndex;
        sBatchFirstIndex = 0;
        sBatchBlocks = 0;
        sBatchBytes = 0;
        return 0;
    }
    /*
     * 整批一次 WriteAt 会长时间不泵 lwIP。按块写并夹 Breath（有线大文件）。
     */
    {
        UINT32 Done = 0;

        while (Done < sBatchBytes) {
            UINT32 Take = sBatchBytes - Done;

            if (Take > KSEND_BLOCK_BYTES) {
                Take = KSEND_BLOCK_BYTES;
            }
            BreathWhileWired();
            if (KsendTransportUserAborted()) {
                Run->Failure = KSEND_FAIL_ABORT;
                Run->Error = FAT_ERR_IO;
                Run->FailBlock = sBatchFirstIndex + (Done / KSEND_BLOCK_BYTES);
                sBatchFirstIndex = 0;
                sBatchBlocks = 0;
                sBatchBytes = 0;
                return 0;
            }
            Error = KsendFileWriteAt(Run->TargetPath, Offset + Done,
                                     KsendFileBuffer() + Done, Take);
            if (Error != FAT_OK) {
                Run->Error = Error;
                Run->FailBlock = sBatchFirstIndex + (Done / KSEND_BLOCK_BYTES);
                sBatchFirstIndex = 0;
                sBatchBlocks = 0;
                sBatchBytes = 0;
                return 0;
            }
            Done += Take;
        }
    }
    BreathWhileWired();
    PublishAcksFromBatch();
    sBatchFirstIndex = 0;
    sBatchBlocks = 0;
    sBatchBytes = 0;
    return 1;
}

int KsendReceiveBatchAccept(KSEND_RUN *Run, UINT32 Index, UINT32 Length,
                            const UINT8 *Data, UINT32 BlockCrc,
                            UINT32 *OutAckCount) {
    UINT32 At;
    UINT32 LimitBlocks = BatchLimitBlocks();

    if (OutAckCount != 0) {
        *OutAckCount = 0;
    }
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
    /* 非连续：先刷旧批；调用方发完 ACK 后再以本块重新 Accept */
    if (sBatchBlocks > 0u && Index != sBatchFirstIndex + sBatchBlocks) {
        if (!KsendReceiveBatchFlush(Run)) {
            return 0;
        }
        if (OutAckCount != 0) {
            *OutAckCount = KsendReceiveBatchAckCount();
        }
        return 2;
    }
    if (sBatchBlocks == 0u) {
        sBatchFirstIndex = Index;
    }
    if (sBatchBlocks >= LimitBlocks || sBatchBlocks >= KSEND_WINDOW_MAX) {
        if (!KsendReceiveBatchFlush(Run)) {
            return 0;
        }
        if (OutAckCount != 0) {
            *OutAckCount = KsendReceiveBatchAckCount();
        }
        return 2;
    }
    At = sBatchBlocks * KSEND_BLOCK_BYTES;
    if (At + Length > KSEND_IO_CHUNK_BYTES) {
        Run->Error = FAT_ERR_INVAL;
        Run->FailBlock = Index;
        return 0;
    }
    CopyBlock(KsendFileBuffer() + At, Data, Length);
    sBatchCrc[sBatchBlocks] = BlockCrc;
    sBatchLength[sBatchBlocks] = Length;
    sBatchBlocks++;
    sBatchBytes = At + Length;

    /*
     * 有线 win>1：对端暂时无后续字节则刷（对齐主机窗末等 ACK）。
     * Wi‑Fi/串口 win=1：每块必刷，不抢有线攒批。
     */
    {
        int ShouldFlush = (Length != KSEND_BLOCK_BYTES || sBatchBlocks >= LimitBlocks);

        if (!ShouldFlush && LimitBlocks > 1u && !KsendTransportInputPending()) {
            ShouldFlush = 1;
        }
        if (ShouldFlush) {
            if (!KsendReceiveBatchFlush(Run)) {
                return 0;
            }
            if (OutAckCount != 0) {
                *OutAckCount = KsendReceiveBatchAckCount();
            }
        }
    }
    return 1;
}
