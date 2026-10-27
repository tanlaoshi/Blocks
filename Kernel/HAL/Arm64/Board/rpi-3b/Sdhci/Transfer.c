/*
 * Transfer.c — SDHCI PIO 读块（bd-pi-sd）
 */
#include "Sdhci/Private.h"

int SdhciProgrammedIoReadBlock(UINT8 *Buffer) {
    UINT32 i;
    UINT32 St;

    for (i = 0; i < 128u; i++) {
        UINT32 Spin;
        for (Spin = 0; Spin < SPIN_MAXIMUM; Spin++) {
            St = SdhciRead32(SDHCI_INT_STATUS);
            if ((St & SDHCI_INT_ERROR) != 0) {
                SdhciWrite32(SDHCI_INT_STATUS, St);
                return -1;
            }
            if ((SdhciRead32(SDHCI_PRESENT_STATE) & SDHCI_DATA_AVAILABLE) != 0) {
                break;
            }
            HalCpuRelax();
        }
        if (Spin >= SPIN_MAXIMUM) {
            return -1;
        }
        {
            UINT32 W = SdhciRead32(SDHCI_BUFFER);
            Buffer[i * 4u + 0] = (UINT8)(W);
            Buffer[i * 4u + 1] = (UINT8)(W >> 8);
            Buffer[i * 4u + 2] = (UINT8)(W >> 16);
            Buffer[i * 4u + 3] = (UINT8)(W >> 24);
        }
    }
    for (i = 0; i < SPIN_MAXIMUM; i++) {
        St = SdhciRead32(SDHCI_INT_STATUS);
        if ((St & SDHCI_INT_ERROR) != 0) {
            SdhciWrite32(SDHCI_INT_STATUS, St);
            return -1;
        }
        if ((St & SDHCI_INT_DATA_END) != 0) {
            SdhciWrite32(SDHCI_INT_STATUS, SDHCI_INT_DATA_END | SDHCI_INT_DATA_AVAIL);
            return 0;
        }
        HalCpuRelax();
    }
    return -1;
}

int SdhciReadOneBlock(UINT32 LogicalBlockAddress, UINT8 *Buffer) {
    UINT32 Argument = gSdhciHighCapacity ? LogicalBlockAddress : (LogicalBlockAddress * 512u);
    UINT16 Flags = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX |
                            SDHCI_CMD_DATA);
    UINT16 TransferMode = (UINT16)(SDHCI_TRNS_READ | SDHCI_TRNS_BLK_CNT_EN);

    /* 0x04 BLOCK_SIZE + 0x06 BLOCK_COUNT 同 32 位字 */
    SdhciWrite32(SDHCI_BLOCK_SIZE, 512u | (1u << 16));

    if (SdhciCommandSend(17, Argument, Flags, TransferMode) != 0) {
        return -1;
    }
    return SdhciProgrammedIoReadBlock(Buffer);
}

void SdhciMarkHexadecimal32(const char *Tag, UINT32 Value) {
    char Buffer[9];
    HalSerialBootMark(Tag);
    HalSerialFormatHex(Buffer, Value, 8);
    HalSerialBootMark(Buffer);
}

