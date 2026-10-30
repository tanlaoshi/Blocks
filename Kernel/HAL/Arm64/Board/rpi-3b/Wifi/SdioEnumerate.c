/*
 * SdioEnumerate.c — Arasan SDIO 枚举 + 使能 F1（pwifi-1）
 */
#include "Wifi/Private.h"

UINT32 gWifiRelativeAddress;

static void WifiSdioDumpResetState(const char *Tag) {
    WifiBootMarkHexadecimal32(Tag, SdhciRead8(SDHCI_SOFTWARE_RESET));
    WifiBootMarkHexadecimal32(" pst=", SdhciRead32(SDHCI_PRESENT_STATE));
    WifiBootMarkHexadecimal32(" clk=", SdhciRead16(SDHCI_CLOCK_CONTROL));
    HalSerialBootMark("\n");
}

/*
 * Arasan：无内部钟时 CMD/DATA 软复位位可能永不自清（对表 Linux bcm2835_mmc：
 * 先钟再复位；冷启用 RESET_ALL）。µSD 走 SDHOST，本路独占 Arasan，可用 ALL。
 */
static int WifiSdioResetWithClock(void) {
    if (SdhciSoftwareReset(SDHCI_RESET_CMD) != 0) {
        WifiSdioDumpResetState("Boot: Pi WIFI reset CMD stuck sr=");
        if (SdhciSoftwareReset(SDHCI_RESET_ALL) != 0) {
            WifiSdioDumpResetState("Boot: Pi WIFI RESET_ALL stuck sr=");
            return -1;
        }
        HalSerialBootMark("Boot: Pi WIFI RESET_ALL ok\n");
        return 1; /* 调用方须重配钟 */
    }
    if (SdhciSoftwareReset(SDHCI_RESET_DATA) != 0) {
        WifiSdioDumpResetState("Boot: Pi WIFI reset DATA stuck sr=");
        if (SdhciSoftwareReset(SDHCI_RESET_ALL) != 0) {
            WifiSdioDumpResetState("Boot: Pi WIFI RESET_ALL stuck sr=");
            return -1;
        }
        HalSerialBootMark("Boot: Pi WIFI RESET_ALL ok\n");
        return 1;
    }
    return 0;
}

int WifiSdioHostBringUp(void) {
    int ResetKind;

    if (SdhciHostPrepare() != 0) {
        return -1;
    }

    /* 先电源/中断/识别钟，再软复位（钟域活着复位才能完成） */
    SdhciWrite8(SDHCI_POWER_CONTROL, (UINT8)(SDHCI_POWER_ON | SDHCI_POWER_330));
    SdhciDelayMicroseconds(10000);
    SdhciWrite8(SDHCI_HOST_CONTROL, 0); /* 1-bit */
    SdhciWrite8(SDHCI_TIMEOUT_CONTROL, 0x0E);
    SdhciWrite32(SDHCI_INT_ENABLE, 0xFFFFFFFFu);
    SdhciWrite32(SDHCI_SIGNAL_ENABLE, 0);
    gSdhciTwoTicksMicroseconds = ((2u * 1000000u) / WIFI_IDENTIFY_HERTZ) + 1u;
    if (SdhciClockConfigure(WIFI_IDENTIFY_HERTZ) != 0) {
        HalSerialBootMark("Boot: Pi WIFI clock id fail\n");
        return -1;
    }
    SdhciDelayMicroseconds(2000);

    HalSerialBootMark("Boot: Pi WIFI SDIO reset\n");
    ResetKind = WifiSdioResetWithClock();
    if (ResetKind < 0) {
        HalSerialBootMark("Boot: Pi WIFI softreset timeout\n");
        return -1;
    }
    if (ResetKind > 0) {
        /* RESET_ALL 清钟树：重配 */
        SdhciWrite8(SDHCI_POWER_CONTROL, (UINT8)(SDHCI_POWER_ON | SDHCI_POWER_330));
        SdhciDelayMicroseconds(10000);
        SdhciWrite8(SDHCI_HOST_CONTROL, 0);
        SdhciWrite8(SDHCI_TIMEOUT_CONTROL, 0x0E);
        SdhciWrite32(SDHCI_INT_ENABLE, 0xFFFFFFFFu);
        SdhciWrite32(SDHCI_SIGNAL_ENABLE, 0);
        if (SdhciClockConfigure(WIFI_IDENTIFY_HERTZ) != 0) {
            HalSerialBootMark("Boot: Pi WIFI clock after ALL fail\n");
            return -1;
        }
        SdhciDelayMicroseconds(2000);
    }
    return 0;
}

int WifiSdioEnumerate(void) {
    UINT32 Ocr = 0;
    UINT32 Attempt;
    UINT16 Flags;

    if (SdhciCommandSend(0, 0, SDHCI_CMD_RESP_NONE, 0) != 0) {
        /* GO_IDLE 可无应答 */
    }
    SdhciDelayMicroseconds(10000);

    Flags = SDHCI_CMD_RESP_SHORT; /* R4：无 CRC */
    if (SdhciCommandSend((UINT8)SDIO_COMMAND_IO_SEND_OP_COND, 0, Flags, 0) != 0) {
        WifiBootMarkHexadecimal32("Boot: Pi WIFI CMD5 fail st=", gSdhciLastCommandStatus);
        HalSerialBootMark("\n");
        return -1;
    }
    Ocr = gSdhciResponse[0];
    for (Attempt = 0; Attempt < 20u; Attempt++) {
        if ((Ocr & SDIO_OCR_CARD_READY) != 0) {
            break;
        }
        SdhciDelayMicroseconds(100000u);
        if (SdhciCommandSend((UINT8)SDIO_COMMAND_IO_SEND_OP_COND, SDIO_OCR_VOLTAGE_3V3, Flags,
                             0) != 0) {
            WifiBootMarkHexadecimal32("Boot: Pi WIFI CMD5 retry fail st=",
                                      gSdhciLastCommandStatus);
            HalSerialBootMark("\n");
            return -1;
        }
        Ocr = gSdhciResponse[0];
    }
    if ((Ocr & SDIO_OCR_CARD_READY) == 0) {
        HalSerialBootMark("Boot: Pi WIFI CMD5 timeout\n");
        return -1;
    }
    WifiBootMarkHexadecimal32("Boot: Pi WIFI OCR=", Ocr);
    HalSerialBootMark("\n");

    Flags = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SdhciCommandSend(3, 0, Flags, 0) != 0) {
        HalSerialBootMark("Boot: Pi WIFI CMD3 fail\n");
        return -1;
    }
    gWifiRelativeAddress = gSdhciResponse[0] & 0xFFFF0000u;

    Flags = (UINT16)(SDHCI_CMD_RESP_SHORT_BUSY | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SdhciCommandSend(7, gWifiRelativeAddress, Flags, 0) != 0) {
        HalSerialBootMark("Boot: Pi WIFI CMD7 fail\n");
        return -1;
    }
    return 0;
}

static int WifiSdioOrByte(UINT32 Function, UINT32 RegisterAddress, UINT8 Bits) {
    UINT8 Value = 0;

    if (WifiSdioCommand52Read(Function, RegisterAddress, &Value) != 0) {
        return -1;
    }
    return WifiSdioCommand52Write(Function, RegisterAddress, (UINT8)(Value | Bits));
}

int WifiSdioEnableFunction1(void) {
    UINT32 Attempt;
    UINT8 Ready = 0;
    UINT32 TransferHertz = WIFI_TRANSFER_HERTZ_FALLBACK;
    UINT8 HostControl = SDHCI_WIDTH_4BIT;
    int HighSpeedOk = 0;

    /*
     * Docs/04 刀 A：对表 Source/PI/wifi/board/pi-mmc1-ios.txt
     * （50 MHz / sd high-speed / 4-bit）。先卡侧 EHS，再主机 HISPD，最后抬钟。
     */
    if (WifiSdioOrByte(SDIO_FUNCTION_0, SDIO_CCCR_HIGH_SPEED, 2u) == 0) {
        HighSpeedOk = 1;
        TransferHertz = WIFI_TRANSFER_HERTZ;
        HostControl = (UINT8)(SDHCI_WIDTH_4BIT | SDHCI_HIGH_SPEED);
    } else {
        HalSerialBootMark("Boot: Pi WIFI no HS, stay 25MHz\n");
    }
    if (WifiSdioOrByte(SDIO_FUNCTION_0, SDIO_CCCR_BUS_INTERFACE, 2u) != 0) {
        HalSerialBootMark("Boot: Pi WIFI bus width fail\n");
        return -1;
    }
    SdhciWrite8(SDHCI_HOST_CONTROL, HostControl);
    gSdhciTwoTicksMicroseconds = ((2u * 1000000u) / TransferHertz) + 1u;
    if (gSdhciTwoTicksMicroseconds < 2u) {
        gSdhciTwoTicksMicroseconds = 2u;
    }
    if (SdhciClockConfigure(TransferHertz) != 0) {
        HalSerialBootMark("Boot: Pi WIFI clock xfer fail\n");
        return -1;
    }
    HalSerialBootMark(HighSpeedOk ? "Boot: Pi WIFI clock=50MHz width=4bit HS\n"
                                   : "Boot: Pi WIFI clock=25MHz width=4bit\n");

    if (WifiSdioCommand52Write(SDIO_FUNCTION_0, SDIO_FBR1_BLOCK_SIZE, 64u) != 0 ||
        WifiSdioCommand52Write(SDIO_FUNCTION_0, SDIO_FBR1_BLOCK_SIZE + 1u, 0) != 0) {
        HalSerialBootMark("Boot: Pi WIFI FBR1 blk fail\n");
        return -1;
    }
    if (WifiSdioOrByte(SDIO_FUNCTION_0, SDIO_CCCR_IO_ENABLE, (UINT8)(1u << SDIO_FUNCTION_1)) !=
        0) {
        HalSerialBootMark("Boot: Pi WIFI IO_ENABLE fail\n");
        return -1;
    }
    (void)WifiSdioCommand52Write(SDIO_FUNCTION_0, SDIO_CCCR_INT_ENABLE, 0);

    for (Attempt = 0; Attempt < 20u; Attempt++) {
        if (WifiSdioCommand52Read(SDIO_FUNCTION_0, SDIO_CCCR_IO_READY, &Ready) != 0) {
            return -1;
        }
        if ((Ready & (1u << SDIO_FUNCTION_1)) != 0) {
            HalSerialBootMark("Boot: Pi WIFI F1 ready\n");
            return 0;
        }
        SdhciDelayMicroseconds(100000u);
    }
    HalSerialBootMark("Boot: Pi WIFI F1 not ready\n");
    return -1;
}

int WifiSdioEnableFunction2(void) {
    UINT8 Value = 0;

    if (WifiSdioCommand52Write(SDIO_FUNCTION_0, SDIO_FBR2_BLOCK_SIZE,
                               (UINT8)(WIFI_WLAN_BLOCK_BYTES & 0xFFu)) != 0 ||
        WifiSdioCommand52Write(SDIO_FUNCTION_0, SDIO_FBR2_BLOCK_SIZE + 1u,
                               (UINT8)((WIFI_WLAN_BLOCK_BYTES >> 8) & 0xFFu)) != 0) {
        HalSerialBootMark("Boot: Pi WIFI FBR2 blk fail\n");
        return -1;
    }
    if (WifiSdioCommand52Read(SDIO_FUNCTION_0, SDIO_CCCR_IO_ENABLE, &Value) != 0) {
        return -1;
    }
    Value = (UINT8)(Value | (1u << SDIO_FUNCTION_1) | (1u << SDIO_FUNCTION_2));
    if (WifiSdioCommand52Write(SDIO_FUNCTION_0, SDIO_CCCR_IO_ENABLE, Value) != 0) {
        HalSerialBootMark("Boot: Pi WIFI F2 enable fail\n");
        return -1;
    }
    return 0;
}
