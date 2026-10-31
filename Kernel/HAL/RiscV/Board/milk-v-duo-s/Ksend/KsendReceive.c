/*
 * KsendReceive.c — 收块状态机：块头 / 数据 / CRC / 落盘 / 'A' 或 'N'
 *
 * 一块的判定顺序是固定的：先看块头合法，再校验数据 CRC，最后才写卡。
 * 任何一步不过都回 'N'，让 PC 重发同一块；同一块回 'N' 超过 3 次就收手，
 * 交给 KsendTransfer 决定回滚。
 */
#include "Ksend/Private.h"

static UINT8 sBlockBuffer[KSEND_BLOCK_BYTES];

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

/* 1=已写卡（回 'A'）；0=已回 'N' 等重发；-1=重试超限/超时 */
static int ReceiveOneBlock(KSEND_RUN *Run, UINT32 Index, UINT32 Length,
                           UINT32 WantCrc, UINT32 *InOutRetry) {
    UINT32 Got;
    int Err;

    if (!ReadBlockData(Length)) {
        Run->Failure = KSEND_FAIL_HEADER;
        return -1;
    }
    Got = KsendCrc32Final(
        KsendCrc32Update(KSEND_CRC_INITIAL, sBlockBuffer, Length));
    if (Got == WantCrc) {
        Run->Dirty = 1; /* 落盘失败也可能写了半块：先记脏，失败路径必须回滚 */
        Err = KsendFileWriteBlock(Run->TargetPath, Index, sBlockBuffer, Length);
        if (Err == FAT_OK) {
            KsendSerialPutByte(KSEND_ACK_WRITTEN);
            AcceptBlock(Run, Index, Length, WantCrc);
            return 1;
        }
        Run->Err = Err;
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
            return 0;
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

