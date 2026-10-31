/*
 * SdioTransfer.c — SDIO CMD52 / CMD53（pwifi-1）
 */
#include "Wifi/Private.h"

/* DAT0 忙等耗时只打前几次，避免刷屏 */
static UINT32 gWifiSdioWaitReported = 0;

/* 固件下载开始前清掉额度，避免被前面的寄存器写占用 */
void WifiSdioResetWaitReport(void) {
    gWifiSdioWaitReported = 0;
}

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

void WifiSdioProgrammedIoFailCleanup(void) {
    gSdhciLastCommandStatus = SdhciRead32(SDHCI_INT_STATUS);
    gSdhciLastPresentState = SdhciRead32(SDHCI_PRESENT_STATE);
    /* 失败即恢复：否则 DATA_INHIBIT 会钉住后面每一条命令 */
    (void)WifiSdioHostCommandDataRecover();
}

/* 读失败必须说清是「哪个阶段、看到什么」：命令级/数据级/收尾级是三条不同的路 */
static void WifiSdioReportReadFailure(const char *Stage) {
    HalSerialBootMarkForce("Boot: Pi WIFI sb read ");
    HalSerialBootMarkForce(Stage);
    WifiBootMarkHexadecimal32(" r5=", gSdhciResponse[0]);
    WifiBootMarkHexadecimal32(" isr=", SdhciRead32(SDHCI_INT_STATUS));
    WifiBootMarkHexadecimal32(" pst=", SdhciRead32(SDHCI_PRESENT_STATE));
    HalSerialBootMarkForce("\n");
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
                WifiSdioReportReadFailure("data-error");
                WifiSdioProgrammedIoFailCleanup();
                return -1;
            }
            if ((SdhciRead32(SDHCI_PRESENT_STATE) & SDHCI_DATA_AVAILABLE) != 0) {
                break;
            }
            SdhciDelayMicroseconds(100);
        }
        if (Wait >= WAIT_MILLISECONDS * 10u) {
            WifiSdioReportReadFailure("no-data");
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
    WifiSdioReportReadFailure("no-end");
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
        WifiSdioReportReadFailure("cmd");
        return -1;
    }
    /* R5_STATUS 对表 Linux sdio.h：15/14/11/9/8；原先未查会吞掉卡侧拒收 */
    if ((gSdhciResponse[0] & 0xCB00u) != 0) {
        /* 卡侧拒收时必须把 R5 打出来，否则「立刻失败」和「挂住」分不清 */
        WifiBootMarkHexadecimal32("Boot: Pi WIFI sb read r5=", gSdhciResponse[0]);
        WifiBootMarkHexadecimal32(" reg=", RegisterAddress);
        HalSerialBootMarkForce("\n");
        WifiSdioProgrammedIoFailCleanup();
        return -1;
    }
    return WifiSdioProgrammedIoReadBytes(Buffer, ByteCount);
}


/*
 * 写块完成后卡在 DAT0 上拉 busy（SRAM 实写有延迟）。对表 SD Host Controller Spec：
 * PRESENT_STATE bit2 DAT_LINE_ACTIVE=0 才是线空。不等它就发下一条 CMD53，
 * 卡会以 R5 ERROR + IO_CURRENT_STATE=CMD 直接拒收（本板实测 @00000040）。
 * 对表 Linux mmc_io_rw_extended：这里应当阻塞等待，而不是当作失败。
 */
int WifiSdioWaitCardIdle(void) {
    UINT32 Spin;

    for (Spin = 0; Spin < SPIN_MAXIMUM; Spin++) {
        if ((SdhciRead32(SDHCI_PRESENT_STATE) & WIFI_HOST_DAT_LINE_ACTIVE) == 0) {
            return 0;
        }
        HalCpuRelax();
    }
    return -1;
}

/*
 * 墙钟版等 DAT0 释放。ALP 域（32.768 kHz）下卡的背板写可能远慢于自旋窗口，
 * Linux 那边是靠 mmc 核心的长超时自然等到，我们之前几毫秒就放弃了。
 * 返回已经等过的毫秒数；超时返回 -1。
 */
int WifiSdioWaitCardIdleMilliseconds(UINT32 Milliseconds) {
    UINT32 Elapsed;

    for (Elapsed = 0; Elapsed < Milliseconds; Elapsed++) {
        if ((SdhciRead32(SDHCI_PRESENT_STATE) & WIFI_HOST_DAT_LINE_ACTIVE) == 0) {
            return (int)Elapsed;
        }
        SdhciDelayMicroseconds(1000);
    }
    return -1;
}

int WifiSdioCommand53Write(UINT32 Function, UINT32 RegisterAddress, const UINT8 *Buffer,
                           UINT32 ByteCount, int AddressIncrement) {
    UINT16 Flags;
    UINT16 TransferMode;
    UINT32 Argument;
    UINT32 Attempt;

    if (Buffer == 0 || ByteCount == 0 || ByteCount > 512u) {
        return -1;
    }
    Argument = ((Function & 7u) << 28) | ((RegisterAddress & 0x1FFFFu) << 9) |
               (ByteCount & 0x1FFu) | (1u << 31);
    if (AddressIncrement) {
        Argument |= (1u << 26);
    }
    Flags = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX | SDHCI_CMD_DATA);
    /*
     * 写：bit4=0。失败 14 / Linux ftrace（blocks=1 block_size=4）：须 BLK_CNT_EN，
     * 否则单块数据阶段可不收尾（与 SRAM no data end 同形）。
     */
    TransferMode = (UINT16)SDHCI_TRNS_BLK_CNT_EN;
    /*
     * 卡刚写完上一块时 function busy，R5 只置 ERROR（无 OUT_OF_RANGE/CRC）。
     * 这是瞬态，按 Linux mmc_io_rw_extended 的语义阻塞等线空后重发，不算失败。
     */
    for (Attempt = 0; Attempt < 8u; Attempt++) {
        int Waited = WifiSdioWaitCardIdleMilliseconds(3000u);

        if (Waited >= 0 && gWifiSdioWaitReported < 6) {
            gWifiSdioWaitReported++;
            WifiBootMarkHexadecimal32("Boot: Pi WIFI dat0 idle ms=", (UINT32)Waited);
            HalSerialBootMarkForce("\n");
        }
        if (Waited < 0) {
            gWifiSdioWaitReported++;
            HalSerialBootMarkForce("Boot: Pi WIFI dat0 busy over 3000ms\n");
            break;
        }
        if (Attempt != 0) {
            SdhciDelayMicroseconds(1000);
        }
        SdhciWrite32(SDHCI_BLOCK_SIZE, ByteCount | (1u << 16));
        if (SdhciCommandSend((UINT8)SDIO_COMMAND_IO_RW_EXTENDED, Argument, Flags,
                             TransferMode) != 0) {
            continue;
        }
        if ((gSdhciResponse[0] & WIFI_SDIO_R5_ERROR_MASK) != 0) {
            if ((gSdhciResponse[0] & WIFI_SDIO_R5_ERROR_MASK) == WIFI_SDIO_R5_ERROR_ONLY) {
                continue;
            }
            WifiSdioProgrammedIoFailCleanup();
            return -1;
        }
        return WifiSdioProgrammedIoWriteBytes(Buffer, ByteCount);
    }
    WifiSdioReportReadFailure("no-end");
    WifiSdioProgrammedIoFailCleanup();
    return -1;
}
