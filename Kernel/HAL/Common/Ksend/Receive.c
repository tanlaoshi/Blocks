/*
 * Receive.c — 收块状态机：块头 / 数据 / CRC / 批写 / 'A' 或 'N'
 *
 * 一块的判定顺序是固定的：先看块头合法，再校验数据 CRC，最后才写卡。
 * 任何一步不过都回 'N'，让 PC 重发同一块；同一块回 'N' 超过 3 次就收手，
 * 交给 KsendTransfer 决定回滚。
 *
 * 批写（v4）：连续块攒进 KsendFileBuffer() 再写，压 FAT 链步数；
 * 'A'=已进批缓冲；整文件 flush+读回仍在 OK 前，失败回滚。
 */
#include "Ksend/Private.h"
#include "HalDevices.h"
#include "LwIp.h"
#include "Scheduler.h"

#define KSEND_BATCH_LIMIT_BYTES KSEND_IO_CHUNK_BYTES

static UINT8 sBlockBuffer[KSEND_BLOCK_BYTES];
/* 批缓冲里连续块的起点/块数/字节数；0 块表示空 */
static UINT32 sBatchFirstIndex;
static UINT32 sBatchBlocks;
static UINT32 sBatchBytes;

static void BatchReset(void) {
    sBatchFirstIndex = 0;
    sBatchBlocks = 0;
    sBatchBytes = 0;
}

/* Wi‑Fi 主槽：每块落盘，减 Sdhost PIO 饿 RX → reason=header（Pi 契约） */
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

/*
 * 把攒下的连续块写卡。按 4 KB 切片并在片间 LwIpService——Pi Sdhost PIO 一次
 * 64 KB 会饿死 Wi‑Fi/有线 RX，TCP 流错位后下一块 MAGIC 对不上 → reason=header。
 */
static int BatchFlush(KSEND_RUN *Run) {
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
            BatchReset();
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
    BatchReset();
    return 1;
}

/*
 * 收一块进批缓冲。'A' 必须在本函数返回后立刻发出（见 ReceiveOneBlock），
 * 故满批/不连续时只 flush「已经 A 过的旧批」，再接纳本块；禁止在接纳本块后
 * 再同步刷盘——否则每 ~512KB 会把 ACK 拖死，Pi 串口大传必超时（2026-11-02）。
 * 短块（最后一块）仍立刻 flush；Wi‑Fi 主槽每满块也 flush（BatchLimitBytes）。
 */
static int BatchAccept(KSEND_RUN *Run, UINT32 Index, UINT32 Length) {
    UINT32 At;
    UINT32 Limit = BatchLimitBytes();

    if (sBatchBlocks > 0u &&
        (Index != sBatchFirstIndex + sBatchBlocks ||
         Length != KSEND_BLOCK_BYTES)) {
        if (!BatchFlush(Run)) {
            return 0;
        }
    }
    if (sBatchBlocks > 0u && (sBatchBytes + Length) > Limit) {
        if (!BatchFlush(Run)) {
            return 0;
        }
    }
    if (sBatchBlocks == 0u) {
        sBatchFirstIndex = Index;
    }
    At = (Index - sBatchFirstIndex) * KSEND_BLOCK_BYTES;
    CopyBlock(KsendFileBuffer() + At, sBlockBuffer, Length);
    sBatchBlocks++;
    sBatchBytes = At + Length;
    if (Length != KSEND_BLOCK_BYTES || sBatchBytes >= Limit) {
        if (!BatchFlush(Run)) {
            return 0;
        }
    }
    return 1;
}

static void AcceptBlock(KSEND_RUN *Run, UINT32 Index, UINT32 Length, UINT32 Crc) {
    UINT32 End = Index * KSEND_BLOCK_BYTES + Length;

    gKsendNewImage.BlockCrc[Index] = Crc;
    Run->Written++;
    if (End > Run->NewSize) {
        Run->NewSize = End;
    }
}

/* 读块头；滑窗对齐 MAGIC（Pi Wi‑Fi 表后杂字节） */
static int ReadHeader(UINT32 *Header) {
    UINT32 Index;
    UINT8 Window[4];
    UINT32 Have = 0;
    UINT8 MagicByte0 = (UINT8)(KSEND_MAGIC & KSEND_BYTE_MASK);

    for (Index = 0; Index < 4u; Index++) {
        Header[Index] = 0;
    }
    for (;;) {
        UINT8 Value;

        if (!KsendSerialGetByte(&Value, KSEND_HEADER_TIMEOUT_MILLISECONDS)) {
            return 0;
        }
        if (Have == 0u && Value != MagicByte0) {
            continue;
        }
        Window[Have++] = Value;
        if (Have < 4u) {
            continue;
        }
        {
            UINT32 Magic = (UINT32)Window[0] | ((UINT32)Window[1] << 8) |
                           ((UINT32)Window[2] << 16) | ((UINT32)Window[3] << 24);

            if (Magic != KSEND_MAGIC) {
                Window[0] = Window[1];
                Window[1] = Window[2];
                Window[2] = Window[3];
                Have = 3u;
                continue;
            }
            Header[0] = Magic;
        }
        break;
    }
    for (Index = 4u; Index < KSEND_HEADER_BYTES; Index++) {
        UINT8 Value;

        if (!KsendSerialGetByte(&Value, KSEND_DATA_TIMEOUT_MILLISECONDS)) {
            return 0;
        }
        Header[Index / KSEND_WORD_BYTES] |=
            (UINT32)Value << ((Index % KSEND_WORD_BYTES) * KSEND_BYTE_BITS);
    }
    return 1;
}

static int ReadBlockData(UINT32 Length) {
    UINT32 Index;

    for (Index = 0; Index < Length; Index++) {
        if (!KsendSerialGetByte(&sBlockBuffer[Index], KSEND_DATA_TIMEOUT_MILLISECONDS)) {
            return 0;
        }
    }
    return 1;
}

/* 1=继续收数据；0=收尾帧已处理；-1=失败或 PC 放弃 */
static int HandleHeader(const UINT32 *Header, KSEND_RUN *Run, UINT32 *OutIndex,
                        UINT32 *OutLength) {
    if (Header[0] != KSEND_MAGIC) {
        Run->Failure = KSEND_FAIL_HEADER;
        return -1;
    }
    if (Header[1] == KSEND_TRAILER_INDEX) {
        if (Header[2] != 0u) {
            Run->Failure = KSEND_FAIL_HEADER;
            return -1;
        }
        Run->TrailerCrc = Header[3];
        return 0;
    }
    if (Header[1] == KSEND_ABORT_INDEX) {
        Run->Failure = KSEND_FAIL_ABORT;
        return -1;
    }
    if (Header[1] >= KSEND_MAX_BLOCKS || Header[2] == 0u ||
        Header[2] > KSEND_BLOCK_BYTES) {
        Run->Failure = KSEND_FAIL_HEADER;
        return -1;
    }
    *OutIndex = Header[1];
    *OutLength = Header[2];
    return 1;
}

/* 1=已收进批缓冲（回 'A'）；0=已回 'N' 等重发；-1=重试超限/超时 */
static int ReceiveOneBlock(KSEND_RUN *Run, UINT32 Index, UINT32 Length,
                           UINT32 WantCrc, UINT32 *InOutRetry) {
    UINT32 Got;

    if (!ReadBlockData(Length)) {
        Run->Failure = KSEND_FAIL_DATA;
        return -1;
    }
    Got = KsendCrc32Final(
        KsendCrc32Update(KSEND_CRC_INITIAL, sBlockBuffer, Length));
    if (Got == WantCrc) {
        /* 批写失败也先记脏：已经 flush 的块可能只写了一部分，失败路径要回滚 */
        Run->Dirty = 1;
        if (!BatchAccept(Run, Index, Length)) {
            /* 失败的是「已经回 A 的批」或本块：重发同一块补不回来，直接收手 */
            Run->Failure = KSEND_FAIL_WRITE;
            KsendSerialPutByte(KSEND_ACK_RETRY);
            return -1;
        }
        KsendSerialPutByte(KSEND_ACK_WRITTEN);
        AcceptBlock(Run, Index, Length, WantCrc);
        return 1;
    }
    KsendSerialPutByte(KSEND_ACK_RETRY);
    (*InOutRetry)++;
    if (*InOutRetry > KSEND_RETRY_MAX) {
        Run->Failure = (Got == WantCrc) ? KSEND_FAIL_WRITE : KSEND_FAIL_VERIFY;
        Run->FailBlock = Index;
        Run->FailWant = WantCrc;
        Run->FailGot = Got;
        return -1;
    }
    return 0;
}

int KsendReceiveBlocks(KSEND_RUN *Run) {
    UINT32 Header[4];
    UINT32 Retry = 0;
    UINT32 RetryIndex = KSEND_MAX_BLOCKS;

    BatchReset();
    for (;;) {
        UINT32 Index = 0;
        UINT32 Length = 0;
        int Rc;

        if (!ReadHeader(Header)) {
            Run->Failure = KSEND_FAIL_HEADER;
            Run->Error = 1; /* timeout/short → FAIL err= */
            KsendNote("receive: header timeout/short");
            return -1;
        }
        Rc = HandleHeader(Header, Run, &Index, &Length);
        if (Rc == 0) {
            return BatchFlush(Run) ? 0 : -1;
        }
        if (Rc < 0) {
            if (Run->Failure == KSEND_FAIL_HEADER) {
                Run->Error = 2; /* magic/字段 */
                KsendNote("receive: header magic/field");
            }
            return -1;
        }
        if (RetryIndex != Index) {
            RetryIndex = Index;
            Retry = 0;
        }
        if (ReceiveOneBlock(Run, Index, Length, Header[3], &Retry) < 0) {
            return -1;
        }
    }
}
