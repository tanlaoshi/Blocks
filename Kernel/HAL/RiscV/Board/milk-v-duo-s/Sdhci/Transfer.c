/*
 * Transfer.c — Duo SDHCI PIO 读写块（bd-duo-sd）
 */
#include "Sdhci/Private.h"

int SdhciProgrammedIoRead(UINT8 *Buffer) {
    UINT32 i;

    if (SdhciWaitMilliseconds32(SDHCI_INT_STATUS, SDHCI_INT_DATA_AVAIL | SDHCI_INT_ERROR, 1,
                 500) != 0) {
        return -1;
    }
    if ((SdhciRead32(SDHCI_INT_STATUS) & SDHCI_INT_ERROR) != 0) {
        gSdhciLastStatus = SdhciRead32(SDHCI_INT_STATUS);
        SdhciWrite32(SDHCI_INT_STATUS, gSdhciLastStatus);
        return -1;
    }
    SdhciWrite32(SDHCI_INT_STATUS, SDHCI_INT_DATA_AVAIL);
    for (i = 0; i < 128u; i++) {
        UINT32 W = SdhciRead32(SDHCI_BUFFER);
        Buffer[i * 4u + 0] = (UINT8)W;
        Buffer[i * 4u + 1] = (UINT8)(W >> 8);
        Buffer[i * 4u + 2] = (UINT8)(W >> 16);
        Buffer[i * 4u + 3] = (UINT8)(W >> 24);
    }
    if (SdhciWaitMilliseconds32(SDHCI_INT_STATUS, SDHCI_INT_DATA_END | SDHCI_INT_ERROR, 1,
                 500) != 0) {
        return -1;
    }
    if ((SdhciRead32(SDHCI_INT_STATUS) & SDHCI_INT_ERROR) != 0) {
        gSdhciLastStatus = SdhciRead32(SDHCI_INT_STATUS);
        SdhciWrite32(SDHCI_INT_STATUS, gSdhciLastStatus);
        return -1;
    }
    SdhciWrite32(SDHCI_INT_STATUS, SDHCI_INT_DATA_END);
    return 0;
}

int SdhciReadOneBlock(UINT32 LogicalBlockAddress, UINT8 *Buffer) {
    UINT32 Argument = gSdhciHighCapacity ? LogicalBlockAddress : (LogicalBlockAddress * 512u);
    UINT16 Flags = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX |
                            SDHCI_CMD_DATA);
    UINT16 TransferMode = (UINT16)(SDHCI_TRNS_READ | SDHCI_TRNS_BLK_CNT_EN);

    SdhciWrite32(SDHCI_BLOCK_SIZE, 512u | (1u << 16));
    if (SdhciCommandSend(17, Argument, Flags, TransferMode) != 0) {
        return -1;
    }
    return SdhciProgrammedIoRead(Buffer);
}

/* SDHCI：Buffer Write Ready → 填 BUFFER → Transfer Complete（单块 CMD24） */
int SdhciProgrammedIoWrite(const UINT8 *Buffer) {
    UINT32 i;

    if (SdhciWaitMilliseconds32(SDHCI_INT_STATUS, SDHCI_INT_SPACE_AVAIL | SDHCI_INT_ERROR, 1,
                 500) != 0) {
        gSdhciLastStatus = SdhciRead32(SDHCI_INT_STATUS);
        return -1;
    }
    if ((SdhciRead32(SDHCI_INT_STATUS) & SDHCI_INT_ERROR) != 0) {
        gSdhciLastStatus = SdhciRead32(SDHCI_INT_STATUS);
        SdhciWrite32(SDHCI_INT_STATUS, gSdhciLastStatus);
        return -1;
    }
    SdhciWrite32(SDHCI_INT_STATUS, SDHCI_INT_SPACE_AVAIL);
    for (i = 0; i < 128u; i++) {
        UINT32 W = (UINT32)Buffer[i * 4u + 0] |
                   ((UINT32)Buffer[i * 4u + 1] << 8) |
                   ((UINT32)Buffer[i * 4u + 2] << 16) |
                   ((UINT32)Buffer[i * 4u + 3] << 24);
        SdhciWrite32(SDHCI_BUFFER, W);
    }
    if (SdhciWaitMilliseconds32(SDHCI_INT_STATUS, SDHCI_INT_DATA_END | SDHCI_INT_ERROR, 1,
                 2000) != 0) {
        gSdhciLastStatus = SdhciRead32(SDHCI_INT_STATUS);
        return -1;
    }
    if ((SdhciRead32(SDHCI_INT_STATUS) & SDHCI_INT_ERROR) != 0) {
        gSdhciLastStatus = SdhciRead32(SDHCI_INT_STATUS);
        SdhciWrite32(SDHCI_INT_STATUS, gSdhciLastStatus);
        return -1;
    }
    SdhciWrite32(SDHCI_INT_STATUS, SDHCI_INT_DATA_END);
    return 0;
}

int SdhciWriteOneBlock(UINT32 LogicalBlockAddress, const UINT8 *Buffer) {
    UINT32 Argument = gSdhciHighCapacity ? LogicalBlockAddress : (LogicalBlockAddress * 512u);
    UINT16 Flags = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX |
                            SDHCI_CMD_DATA);
    /* 写：TRANSFER_MODE 不置 READ */
    UINT16 TransferMode = SDHCI_TRNS_BLK_CNT_EN;

    SdhciWrite32(SDHCI_BLOCK_SIZE, 512u | (1u << 16));
    if (SdhciCommandSend(24, Argument, Flags, TransferMode) != 0) {
        return -1;
    }
    return SdhciProgrammedIoWrite(Buffer);
}
