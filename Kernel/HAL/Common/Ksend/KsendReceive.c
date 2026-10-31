/*
 * KsendReceive.c — 收块状态机：块头 / 数据 / CRC / 批写 / 'A' 或 'N'
 *
 * 一块的判定顺序是固定的：先看块头合法，再校验数据 CRC，最后才写卡。
 * 任何一步不过都回 'N'，让 PC 重发同一块；同一块回 'N' 超过 3 次就收手，
 * 交给 KsendTransfer 决定回滚。
 *
 * 为什么要批写（v4）：PC 按 4 KB 块发差量，而 FAT 没有反向链指针，
 * 每次 FileSystemWriteFileAt 都要从首簇走到偏移（O(偏移/簇)）；逐块写
 * 2.1 MB 就是第二层 O(n²)，只修分配器也还是慢。这里把连续的块攒进
 * KsendFileBuffer()，一次写 512 KB，把链步数压到 1/128。
 * 代价：'A' 的含义从「已落盘」变成「已收进批缓冲」；整文件 flush + 读回
 * 校验仍然在 KSEND OK 之前，失败照样回滚。
 */
#include "Ksend/Private.h"

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

static void CopyBlock(UINT8 *Destination, const UINT8 *Source, UINT32 Length) {
    UINT32 Index;

    for (Index = 0; Index < Length; Index++) {
        Destination[Index] = Source[Index];
    }
}

/* 把攒下的连续块一次写卡；失败填 Run 的失败原因与块号 */
static int BatchFlush(KSEND_RUN *Run) {
    UINT32 Offset;
    int Err;

    if (sBatchBytes == 0u) {
        return 1;
    }
    Offset = sBatchFirstIndex * KSEND_BLOCK_BYTES;
    Err = KsendFileWriteAt(Run->TargetPath, Offset, KsendFileBuffer(),
                           sBatchBytes);
    if (Err != FAT_OK) {
        Run->Err = Err;
        Run->FailBlock = sBatchFirstIndex + sBatchBlocks - 1u;
        BatchReset();
        return 0;
    }
    BatchReset();
    return 1;
}

/* 收一块进批缓冲；不连续或末块（短块）先把前面的批 flush 掉 */
static int BatchAccept(KSEND_RUN *Run, UINT32 Index, UINT32 Length) {
    UINT32 At;

    if (sBatchBlocks > 0u &&
        (Index != sBatchFirstIndex + sBatchBlocks ||
         Length != KSEND_BLOCK_BYTES)) {
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
    if (sBatchBytes + KSEND_BLOCK_BYTES > KSEND_BATCH_LIMIT_BYTES ||
        Length != KSEND_BLOCK_BYTES) {
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

/* 读满 4 个字（小端）；第一个字节给足 30 s，其余按逐字节 5 s */
static int ReadHeader(UINT32 *Header) {
    UINT32 Index;

    for (Index = 0; Index < 4u; Index++) {
        Header[Index] = 0;
    }
    for (Index = 0; Index < KSEND_HEADER_BYTES; Index++) {
        UINT8 Value;
        UINT32 Timeout = (Index == 0u) ? KSEND_HEADER_TIMEOUT_MS
                                       : KSEND_DATA_TIMEOUT_MS;

        if (!KsendSerialGetByte(&Value, Timeout)) {
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
        if (!KsendSerialGetByte(&sBlockBuffer[Index], KSEND_DATA_TIMEOUT_MS)) {
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
        Run->Failure = KSEND_FAIL_HEADER;
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
            return -1;
        }
        Rc = HandleHeader(Header, Run, &Index, &Length);
        if (Rc == 0) {
            /* 收尾帧：最后一批必须先落盘，后面的 flush/读回校验才有意义 */
            return BatchFlush(Run) ? 0 : -1;
        }
        if (Rc < 0) {
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
