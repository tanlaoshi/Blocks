/*
 * Receive.c — 收块状态机：块头 / 数据 / CRC / 'A' 或 'N'（批写见 ReceiveBatch）
 *
 * 一块：先块头合法，再数据 CRC，再进批缓冲。不过回 'N'；超限交给 Transfer 回滚。
 * TCP 滑窗（nuc-1）：'N' 后丢弃其它 index 载荷（不回 A），直至重收到该块。
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
        Run->Dirty = 1;
        if (!KsendReceiveBatchAccept(Run, Index, Length, sBlockBuffer)) {
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
    UINT32 ExpectRetransmit = KSEND_MAX_BLOCKS;

    KsendReceiveBatchReset();
    for (;;) {
        UINT32 Index = 0;
        UINT32 Length = 0;
        int Rc;
        int BlockRc;

        if (!ReadHeader(Header)) {
            Run->Failure = KSEND_FAIL_HEADER;
            Run->Error = 1;
            KsendNote("receive: header timeout/short");
            return -1;
        }
        Rc = HandleHeader(Header, Run, &Index, &Length);
        if (Rc == 0) {
            return KsendReceiveBatchFlush(Run) ? 0 : -1;
        }
        if (Rc < 0) {
            if (Run->Failure == KSEND_FAIL_HEADER) {
                Run->Error = 2;
                KsendNote("receive: header magic/field");
            }
            return -1;
        }
        if (ExpectRetransmit < KSEND_MAX_BLOCKS && Index != ExpectRetransmit) {
            if (!ReadBlockData(Length)) {
                Run->Failure = KSEND_FAIL_DATA;
                return -1;
            }
            continue;
        }
        if (RetryIndex != Index) {
            RetryIndex = Index;
            Retry = 0;
        }
        BlockRc = ReceiveOneBlock(Run, Index, Length, Header[3], &Retry);
        if (BlockRc < 0) {
            return -1;
        }
        if (BlockRc == 0) {
            ExpectRetransmit = Index;
            continue;
        }
        ExpectRetransmit = KSEND_MAX_BLOCKS;
    }
}
