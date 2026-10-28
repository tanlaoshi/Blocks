/*
 * SdioTransfer.c — SDIO CMD52 / CMD53（pwifi-1）
 */
#include "Wifi/Private.h"

void WifiBootMarkHexadecimal32(const char *Tag, UINT32 Value) {
    char Buffer[9];

    HalSerialBootMarkForce(Tag);
    HalSerialFormatHex(Buffer, Value, 8);
    HalSerialBootMarkForce(Buffer);
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

static void WifiSdioProgrammedIoFailCleanup(void) {
    gSdhciLastCommandStatus = SdhciRead32(SDHCI_INT_STATUS);
    gSdhciLastPresentState = SdhciRead32(SDHCI_PRESENT_STATE);
    SdhciWrite32(SDHCI_INT_STATUS, 0xFFFFFFFFu);
    (void)SdhciSoftwareReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
}

int WifiSdioHostCommandDataRecover(void) {
    /* CCCR I/O Abort 只 abort F1；勿置 RES。对表本板 Card.c：只清 CMD|DATA */
    (void)WifiSdioCommand52Write(SDIO_FUNCTION_0, SDIO_CCCR_IO_ABORT,
                                 (UINT8)SDIO_FUNCTION_1);
    if (SdhciSoftwareReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI host cmd|data reset fail\n");
        return -1;
    }
    SdhciWrite32(SDHCI_INT_STATUS, 0xFFFFFFFFu);
    WifiBackplaneWindowInvalidate();
    SdhciDelayMicroseconds(1000);
    return 0;
}

static int WifiSdioProgrammedIoReadBytes(UINT8 *Buffer, UINT32 ByteCount) {
    UINT32 Words = (ByteCount + 3u) / 4u;
    UINT32 Index;
    UINT32 Status;
    UINT32 Wait;

    for (Index = 0; Index < Words; Index++) {
        /* SPIN_MAXIMUM 在 A53 上仅数 ms；读用墙钟（对表 SdhciWaitMilliseconds） */
        for (Wait = 0; Wait < WAIT_MILLISECONDS * 10u; Wait++) {
            Status = SdhciRead32(SDHCI_INT_STATUS);
            if ((Status & SDHCI_INT_ERROR) != 0) {
                WifiSdioProgrammedIoFailCleanup();
                return -1;
            }
            if ((SdhciRead32(SDHCI_PRESENT_STATE) & SDHCI_DATA_AVAILABLE) != 0) {
                break;
            }
            SdhciDelayMicroseconds(100);
        }
        if (Wait >= WAIT_MILLISECONDS * 10u) {
            WifiSdioProgrammedIoFailCleanup();
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
    for (Wait = 0; Wait < WAIT_MILLISECONDS * 10u; Wait++) {
        Status = SdhciRead32(SDHCI_INT_STATUS);
        if ((Status & SDHCI_INT_ERROR) != 0) {
            WifiSdioProgrammedIoFailCleanup();
            return -1;
        }
        if ((Status & SDHCI_INT_DATA_END) != 0) {
            SdhciWrite32(SDHCI_INT_STATUS, SDHCI_INT_DATA_END | SDHCI_INT_DATA_AVAIL);
            return 0;
        }
        SdhciDelayMicroseconds(100);
    }
    WifiSdioProgrammedIoFailCleanup();
    return -1;
}

int WifiSdioCommand53Read(UINT32 Function, UINT32 RegisterAddress, UINT8 *Buffer,
                          UINT32 ByteCount, int AddressIncrement) {
    UINT16 Flags;
    UINT16 TransferMode;
    UINT32 Argument;

    if (Buffer == 0 || ByteCount == 0 || ByteCount > 512u) {
        return -1;
    }
    /* byte mode；F1 背板递增，F2 WLAN FIFO 固定址 */
    Argument = ((Function & 7u) << 28) | ((RegisterAddress & 0x1FFFFu) << 9) |
               (ByteCount & 0x1FFu);
    if (AddressIncrement) {
        Argument |= (1u << 26);
    }
    Flags = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX | SDHCI_CMD_DATA);
    TransferMode = (UINT16)SDHCI_TRNS_READ;
    /* 与写路径同形：block_size=字节数、block_count=1（本板写已通） */
    SdhciWrite32(SDHCI_BLOCK_SIZE, ByteCount | (1u << 16));
    if (SdhciCommandSend((UINT8)SDIO_COMMAND_IO_RW_EXTENDED, Argument, Flags, TransferMode) !=
        0) {
        return -1;
    }
    return WifiSdioProgrammedIoReadBytes(Buffer, ByteCount);
}

static int WifiSdioProgrammedIoWriteBytes(const UINT8 *Buffer, UINT32 ByteCount) {
    UINT32 Words = (ByteCount + 3u) / 4u;
    UINT32 Index;
    UINT32 Status;

    for (Index = 0; Index < Words; Index++) {
        UINT32 Spin;
        UINT32 Base = Index * 4u;
        UINT32 Word = 0;

        for (Spin = 0; Spin < SPIN_MAXIMUM; Spin++) {
            Status = SdhciRead32(SDHCI_INT_STATUS);
            if ((Status & SDHCI_INT_ERROR) != 0) {
                WifiSdioProgrammedIoFailCleanup();
                return -1;
            }
            if ((SdhciRead32(SDHCI_PRESENT_STATE) & SDHCI_SPACE_AVAILABLE) != 0) {
                break;
            }
            HalCpuRelax();
        }
        if (Spin >= SPIN_MAXIMUM) {
            WifiSdioProgrammedIoFailCleanup();
            return -1;
        }
        if (Base < ByteCount) {
            Word |= (UINT32)Buffer[Base];
        }
        if (Base + 1u < ByteCount) {
            Word |= (UINT32)Buffer[Base + 1u] << 8;
        }
        if (Base + 2u < ByteCount) {
            Word |= (UINT32)Buffer[Base + 2u] << 16;
        }
        if (Base + 3u < ByteCount) {
            Word |= (UINT32)Buffer[Base + 3u] << 24;
        }
        SdhciWrite32(SDHCI_BUFFER, Word);
    }
    for (Index = 0; Index < SPIN_MAXIMUM; Index++) {
        Status = SdhciRead32(SDHCI_INT_STATUS);
        if ((Status & SDHCI_INT_ERROR) != 0) {
            WifiSdioProgrammedIoFailCleanup();
            return -1;
        }
        if ((Status & SDHCI_INT_DATA_END) != 0) {
            SdhciWrite32(SDHCI_INT_STATUS, SDHCI_INT_DATA_END | SDHCI_INT_SPACE_AVAIL);
            return 0;
        }
        HalCpuRelax();
    }
    WifiSdioProgrammedIoFailCleanup();
    return -1;
}

int WifiSdioCommand53Write(UINT32 Function, UINT32 RegisterAddress, const UINT8 *Buffer,
                           UINT32 ByteCount, int AddressIncrement) {
    UINT16 Flags;
    UINT16 TransferMode;
    UINT32 Argument;

    if (Buffer == 0 || ByteCount == 0 || ByteCount > 512u) {
        return -1;
    }
    Argument = ((Function & 7u) << 28) | ((RegisterAddress & 0x1FFFFu) << 9) |
               (ByteCount & 0x1FFu) | (1u << 31);
    if (AddressIncrement) {
        Argument |= (1u << 26);
    }
    Flags = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX | SDHCI_CMD_DATA);
    TransferMode = 0; /* write */
    SdhciWrite32(SDHCI_BLOCK_SIZE, ByteCount | (1u << 16));
    if (SdhciCommandSend((UINT8)SDIO_COMMAND_IO_RW_EXTENDED, Argument, Flags, TransferMode) !=
        0) {
        return -1;
    }
    return WifiSdioProgrammedIoWriteBytes(Buffer, ByteCount);
}
