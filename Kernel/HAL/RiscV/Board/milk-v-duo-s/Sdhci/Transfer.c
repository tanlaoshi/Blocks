/*
 * Transfer.c — Duo SDHCI PIO 数据相（bd-duo-sd）
 *
 * PR-30：原来的单块 512 B 搬运泛化成「按 512 B 分块、块数由调用者给」，
 * SD 卡（CMD17/24）与 SDIO CMD53 共用。SDHCI 的缓冲是 512 B 一块，
 * 每块前等一次 Buffer Read/Write Ready（同 Linux sdhci_transfer_pio 的语义）。
 */
#include "Sdhci/Private.h"

static int SdhciPioAbort(SDHCI_HOST *Host, UINT32 Status) {
    Host->LastStatus = Status;
    Host->LastPresentState = SdhciRead32(Host, SDHCI_PRESENT_STATE);
    SdhciWrite32(Host, SDHCI_INT_STATUS, Status);
    (void)SdhciSoftwareReset(Host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
    return -1;
}

/* 读数据相：Bytes 由调用者按「块大小 × 块数」给；不是 4 的倍数时末尾读整字再截断 */
int SdhciPioRead(SDHCI_HOST *Host, UINT8 *Buffer, UINT32 Bytes) {
    UINT32 Offset = 0;
    UINT32 St;

    if (Buffer == 0 || Bytes == 0) {
        return -1;
    }
    while (Offset < Bytes) {
        UINT32 Chunk = Bytes - Offset;
        UINT32 Words;
        UINT32 i;

        if (Chunk > SDHCI_BLOCK_BYTES) {
            Chunk = SDHCI_BLOCK_BYTES;
        }
        /*
         * 就绪判据用 PRESENT_STATE[11] 而不是中断位：中断位是 W1C，若控制器
         * 内部缓冲 >512 B，清一次会丢掉后续块的「就绪」通知（Linux 同样查状态位）。
         */
        if (SdhciWait32(Host, SDHCI_PRESENT_STATE, SDHCI_BUF_RD_READY, 1, 500) != 0) {
            return SdhciPioAbort(Host, SdhciRead32(Host, SDHCI_INT_STATUS));
        }
        St = SdhciRead32(Host, SDHCI_INT_STATUS);
        if ((St & SDHCI_INT_ERROR) != 0) {
            return SdhciPioAbort(Host, St);
        }
        Words = (Chunk + 3u) / 4u;
        for (i = 0; i < Words; i++) {
            UINT32 W = SdhciRead32(Host, SDHCI_BUFFER);
            UINT32 Left = Chunk - i * 4u;
            UINT8 *P = Buffer + Offset + i * 4u;
            P[0] = (UINT8)W;
            if (Left > 1u) {
                P[1] = (UINT8)(W >> 8);
            }
            if (Left > 2u) {
                P[2] = (UINT8)(W >> 16);
            }
            if (Left > 3u) {
                P[3] = (UINT8)(W >> 24);
            }
        }
        Offset += Chunk;
    }
    if (SdhciWait32(Host, SDHCI_INT_STATUS, SDHCI_INT_DATA_END | SDHCI_INT_ERROR, 1, 500) != 0) {
        return SdhciPioAbort(Host, SdhciRead32(Host, SDHCI_INT_STATUS));
    }
    St = SdhciRead32(Host, SDHCI_INT_STATUS);
    if ((St & SDHCI_INT_ERROR) != 0) {
        return SdhciPioAbort(Host, St);
    }
    SdhciWrite32(Host, SDHCI_INT_STATUS, SDHCI_INT_DATA_END);
    return 0;
}

/* 写数据相 */
int SdhciPioWrite(SDHCI_HOST *Host, const UINT8 *Buffer, UINT32 Bytes) {
    UINT32 Offset = 0;
    UINT32 St;

    if (Buffer == 0 || Bytes == 0) {
        return -1;
    }
    while (Offset < Bytes) {
        UINT32 Chunk = Bytes - Offset;
        UINT32 Words;
        UINT32 i;

        if (Chunk > SDHCI_BLOCK_BYTES) {
            Chunk = SDHCI_BLOCK_BYTES;
        }
        /* 同上：查 PRESENT_STATE[10]「Buffer Write Enable」（空缓冲=1） */
        if (SdhciWait32(Host, SDHCI_PRESENT_STATE, SDHCI_BUF_WR_READY, 1, 500) != 0) {
            return SdhciPioAbort(Host, SdhciRead32(Host, SDHCI_INT_STATUS));
        }
        St = SdhciRead32(Host, SDHCI_INT_STATUS);
        if ((St & SDHCI_INT_ERROR) != 0) {
            return SdhciPioAbort(Host, St);
        }
        Words = (Chunk + 3u) / 4u;
        for (i = 0; i < Words; i++) {
            UINT32 Left = Chunk - i * 4u;
            const UINT8 *P = Buffer + Offset + i * 4u;
            UINT32 W = (UINT32)P[0];
            if (Left > 1u) {
                W |= (UINT32)P[1] << 8;
            }
            if (Left > 2u) {
                W |= (UINT32)P[2] << 16;
            }
            if (Left > 3u) {
                W |= (UINT32)P[3] << 24;
            }
            SdhciWrite32(Host, SDHCI_BUFFER, W);
        }
        Offset += Chunk;
    }
    if (SdhciWait32(Host, SDHCI_INT_STATUS, SDHCI_INT_DATA_END | SDHCI_INT_ERROR, 1, 2000) != 0) {
        return SdhciPioAbort(Host, SdhciRead32(Host, SDHCI_INT_STATUS));
    }
    St = SdhciRead32(Host, SDHCI_INT_STATUS);
    if ((St & SDHCI_INT_ERROR) != 0) {
        return SdhciPioAbort(Host, St);
    }
    SdhciWrite32(Host, SDHCI_INT_STATUS, SDHCI_INT_DATA_END);
    return 0;
}

int SdhciReadOneBlock(SDHCI_HOST *Host, UINT32 LogicalBlockAddress, UINT8 *Buffer) {
    UINT32 Argument =
        Host->HighCapacity ? LogicalBlockAddress : (LogicalBlockAddress * SDHCI_BLOCK_BYTES);
    UINT16 Flags =
        (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX | SDHCI_CMD_DATA);
    UINT16 TransferMode = (UINT16)(SDHCI_TRNS_READ | SDHCI_TRNS_BLK_CNT_EN);

    SdhciWrite32(Host, SDHCI_BLOCK_SIZE, SDHCI_BLOCK_BYTES | (1u << 16));
    if (SdhciCommandSend(Host, 17, Argument, Flags, TransferMode) != 0) {
        return -1;
    }
    return SdhciPioRead(Host, Buffer, SDHCI_BLOCK_BYTES);
}

int SdhciWriteOneBlock(SDHCI_HOST *Host, UINT32 LogicalBlockAddress, const UINT8 *Buffer) {
    UINT32 Argument =
        Host->HighCapacity ? LogicalBlockAddress : (LogicalBlockAddress * SDHCI_BLOCK_BYTES);
    UINT16 Flags =
        (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX | SDHCI_CMD_DATA);
    /* 写：TRANSFER_MODE 不置 READ */
    UINT16 TransferMode = SDHCI_TRNS_BLK_CNT_EN;

    SdhciWrite32(Host, SDHCI_BLOCK_SIZE, SDHCI_BLOCK_BYTES | (1u << 16));
    if (SdhciCommandSend(Host, 24, Argument, Flags, TransferMode) != 0) {
        return -1;
    }
    return SdhciPioWrite(Host, Buffer, SDHCI_BLOCK_BYTES);
}
