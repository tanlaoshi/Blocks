/*
 * SdioTransfer.c — SDIO CMD52 / CMD53（pwifi-1）
 */
#include "Wifi/Private.h"

void WifiBootMarkHexadecimal32(const char *Tag, UINT32 Value) {
    char Buffer[9];

    HalSerialBootMark(Tag);
    HalSerialFormatHex(Buffer, Value, 8);
    HalSerialBootMark(Buffer);
}

static UINT32 WifiSdioDirectArgument(int Write, UINT32 Function, UINT32 RegisterAddress,
                                     UINT8 Data) {
    UINT32 Argument = ((Function & 7u) << 28) | ((RegisterAddress & 0x1FFFFu) << 9) |
                      ((UINT32)Data & 0xFFu);
    if (Write) {
        Argument |= (1u << 31);
    }
    return Argument;
}

int WifiSdioCommand52Read(UINT32 Function, UINT32 RegisterAddress, UINT8 *OutData) {
    UINT16 Flags = SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX;
    UINT32 Argument = WifiSdioDirectArgument(0, Function, RegisterAddress, 0);

    if (OutData == 0) {
        return -1;
    }
    if (SdhciCommandSend((UINT8)SDIO_COMMAND_IO_RW_DIRECT, Argument, Flags, 0) != 0) {
        return -1;
    }
    if ((gSdhciResponse[0] & 0xCF00u) != 0) {
        return -1;
    }
    *OutData = (UINT8)(gSdhciResponse[0] & 0xFFu);
    return 0;
}

int WifiSdioCommand52Write(UINT32 Function, UINT32 RegisterAddress, UINT8 Data) {
    UINT16 Flags = SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX;
    UINT32 Argument = WifiSdioDirectArgument(1, Function, RegisterAddress, Data);
    UINT32 Attempt;

    for (Attempt = 0; Attempt < 10u; Attempt++) {
        if (SdhciCommandSend((UINT8)SDIO_COMMAND_IO_RW_DIRECT, Argument, Flags, 0) != 0) {
            continue;
        }
        if ((gSdhciResponse[0] & 0xCF00u) == 0) {
            return 0;
        }
    }
    return -1;
}

static int WifiSdioProgrammedIoReadBytes(UINT8 *Buffer, UINT32 ByteCount) {
    UINT32 Words = (ByteCount + 3u) / 4u;
    UINT32 Index;
    UINT32 Status;

    for (Index = 0; Index < Words; Index++) {
        UINT32 Spin;
        for (Spin = 0; Spin < SPIN_MAXIMUM; Spin++) {
            Status = SdhciRead32(SDHCI_INT_STATUS);
            if ((Status & SDHCI_INT_ERROR) != 0) {
                SdhciWrite32(SDHCI_INT_STATUS, Status);
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
            UINT32 Word = SdhciRead32(SDHCI_BUFFER);
            UINT32 Base = Index * 4u;
            if (Base < ByteCount) {
                Buffer[Base] = (UINT8)Word;
            }
            if (Base + 1u < ByteCount) {
                Buffer[Base + 1u] = (UINT8)(Word >> 8);
            }
            if (Base + 2u < ByteCount) {
                Buffer[Base + 2u] = (UINT8)(Word >> 16);
            }
            if (Base + 3u < ByteCount) {
                Buffer[Base + 3u] = (UINT8)(Word >> 24);
            }
        }
    }
    for (Index = 0; Index < SPIN_MAXIMUM; Index++) {
        Status = SdhciRead32(SDHCI_INT_STATUS);
        if ((Status & SDHCI_INT_ERROR) != 0) {
            SdhciWrite32(SDHCI_INT_STATUS, Status);
            return -1;
        }
        if ((Status & SDHCI_INT_DATA_END) != 0) {
            SdhciWrite32(SDHCI_INT_STATUS, SDHCI_INT_DATA_END | SDHCI_INT_DATA_AVAIL);
            return 0;
        }
        HalCpuRelax();
    }
    return -1;
}

int WifiSdioCommand53Read(UINT32 Function, UINT32 RegisterAddress, UINT8 *Buffer,
                          UINT32 ByteCount) {
    UINT16 Flags;
    UINT16 TransferMode;
    UINT32 Argument;

    if (Buffer == 0 || ByteCount == 0 || ByteCount > 512u) {
        return -1;
    }
    /* byte mode、地址递增、读 */
    Argument = ((Function & 7u) << 28) | (1u << 26) | ((RegisterAddress & 0x1FFFFu) << 9) |
               (ByteCount & 0x1FFu);
    Flags = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX | SDHCI_CMD_DATA);
    TransferMode = (UINT16)SDHCI_TRNS_READ;
    SdhciWrite32(SDHCI_BLOCK_SIZE, ByteCount | (1u << 16));
    if (SdhciCommandSend((UINT8)SDIO_COMMAND_IO_RW_EXTENDED, Argument, Flags, TransferMode) !=
        0) {
        return -1;
    }
    return WifiSdioProgrammedIoReadBytes(Buffer, ByteCount);
}
