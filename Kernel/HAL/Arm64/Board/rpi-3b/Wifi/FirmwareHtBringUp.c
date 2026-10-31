/*
 * FirmwareHtBringUp.c — 拉 ARM 后等 HT / 开 F2（pi-wifi-5）
 *
 * 实证：bulk 后、ARM/HT 前 CMD53 读会楔死；此文件只 CMD52 + Write32。
 * Linux brcmf_sdio_htclk：整字节写 HT_AVAIL_REQ（0x10），非 RMW。
 */
#include "Wifi/Private.h"

/*
 * 对表 brcmf_sdio_htclk：请求 HT 是把整个字节写成 HT_AVAIL_REQ，
 * FORCE_ALP 与 ALP_AVAIL_REQ 必须一起清掉。保留 FORCE_ALP 会把 ALP 钉死，
 * 芯片永远不会切到 HT。
 */
static int WifiClockRequestHighThroughput(void) {
    UINT8 ClockControl = 0;

    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL,
                               (UINT8)WIFI_CLOCK_REQUEST_HT) != 0) {
        return -1;
    }
    if (WifiSdioCommand52Read(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL, &ClockControl) != 0) {
        return -1;
    }
    WifiBootMarkHexadecimal32("Boot: Pi WIFI HT req csr=", ClockControl);
    HalSerialBootMarkForce("\n");
    return 0;
}


static void WifiCoreStateProbeMark(const char *Name, UINT32 WrapperBase) {
    UINT32 IoControl = 0;
    UINT32 ResetControl = 0;
    int IoRead;
    int ResetRead;

    IoRead = WifiBackplaneRead32(WrapperBase + WIFI_AI_IO_CONTROL_OFFSET, &IoControl);
    ResetRead = WifiBackplaneRead32(WrapperBase + WIFI_AI_RESET_CONTROL_OFFSET, &ResetControl);
    HalSerialBootMarkForce("Boot: Pi WIFI ");
    HalSerialBootMarkForce(Name);
    if (IoRead == 0) {
        WifiBootMarkHexadecimal32(" io=", IoControl);
    } else {
        HalSerialBootMarkForce(" io=fail");
    }
    if (ResetRead == 0) {
        WifiBootMarkHexadecimal32(" rst=", ResetControl);
    } else {
        HalSerialBootMarkForce(" rst=fail");
    }
    HalSerialBootMarkForce("\n");
}

/*
 * 灌数据前先要 HT 并等它真的到手：
 * 寄存器块在常开域（ALP 即可），存储阵列在高速域；若 HT 拿不到，
 * 阵列访问就会像现在这样「发出去、永不回应」。
 */
int WifiClockRequestHighThroughputAndWait(void) {
    UINT8 Status = 0;
    UINT32 Attempt;

    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL,
                               (UINT8)WIFI_CLOCK_REQUEST_HT) != 0) {
        return -1;
    }
    for (Attempt = 0; Attempt < 1500u; Attempt++) {
        if (WifiSdioCommand52Read(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL, &Status) != 0) {
            return -1;
        }
        if ((Status & WIFI_CLOCK_HT_AVAILABLE) != 0) {
            break;
        }
        SdhciDelayMicroseconds(1000);
    }
    WifiBootMarkHexadecimal32("Boot: Pi WIFI HT before sram csr=", Status);
    HalSerialBootMarkForce("\n");
    return ((Status & WIFI_CLOCK_HT_AVAILABLE) != 0) ? 0 : -1;
}

int WifiExitDownload(void) {
    /*
     * 对表 Linux cm3_set_active + cyw43：SOCRAM 已在下刀前拉出复位（见 WifiEnterDownload），
     * 此处只清 intstatus 再拉 ARM —— 灌完后再动 SOCRAM 会把刚写进去的固件一起复位掉。
     */
    if (WifiBackplaneWrite32(WIFI_SDIO_INT_STATUS, 0xFFFFFFFFu) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI intstatus clear fail\n");
        return -1;
    }
    if (WifiCoreReset(WIFI_ARM_WRAPPER, 0, 0, 0) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI ARM reset fail\n");
        return -1;
    }
    HalSerialBootMarkForce("Boot: Pi WIFI ARM reset ok\n");
    SdhciDelayMicroseconds(50000);
    WifiCoreStateProbeMark("socram-after", WIFI_SOCRAM_WRAPPER);
    WifiCoreStateProbeMark("arm-after", WIFI_ARM_WRAPPER);
    if (WifiSdioHostCommandDataRecover() != 0) {
        return -1;
    }
    HalSerialBootMarkForce("Boot: Pi WIFI HT wait begin\n");
    return 0;
}

/*
 * pi-wifi-5：固件已落地后等 HT_AVAILABLE。超时必打 csr 原文 + ARM/SOCRAM 态。
 * 禁止在此加长超时当第一刀；F2 留给 pi-wifi-6。
 */
int WifiWaitHighThroughput(void) {
    UINT32 Attempt;
    UINT8 Status = 0;

    if (WifiClockRequestHighThroughput() != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI HT req fail\n");
        return -1;
    }

    for (Attempt = 0; Attempt < 2000u; Attempt++) {
        if (WifiSdioCommand52Read(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL, &Status) != 0) {
            /* ARM 刚起时偶发：再清主机数据通路后重试 */
            (void)WifiSdioHostCommandDataRecover();
            if (WifiSdioCommand52Read(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL, &Status) !=
                0) {
                WifiBootMarkHexadecimal32("Boot: Pi WIFI HT poll fail csr attempt=", Attempt);
                HalSerialBootMarkForce("\n");
                return -1;
            }
        }
        if ((Status & WIFI_CLOCK_HT_AVAILABLE) != 0) {
            break;
        }
        if ((Attempt % 200u) == 0u) {
            WifiBootMarkHexadecimal32("Boot: Pi WIFI HT wait csr=", Status);
            HalSerialBootMarkForce("\n");
            (void)WifiClockRequestHighThroughput();
        }
        SdhciDelayMicroseconds(1000);
    }
    if ((Status & WIFI_CLOCK_HT_AVAILABLE) == 0) {
        WifiBootMarkHexadecimal32("Boot: Pi WIFI HT timeout csr=", Status);
        HalSerialBootMarkForce("\n");
        WifiCoreStateProbeMark("arm-timeout", WIFI_ARM_WRAPPER);
        WifiCoreStateProbeMark("socram-timeout", WIFI_SOCRAM_WRAPPER);
        return -1;
    }
    WifiBootMarkHexadecimal32("Boot: Pi WIFI HT ok csr=", Status);
    HalSerialBootMarkForce("\n");
    return 0;
}

int WifiWaitHtAndFunction2(void) {
    UINT32 Attempt;
    UINT8 Ready = 0;
    UINT32 ChipCommon = 0;

    if (WifiWaitHighThroughput() != 0) {
        return -1;
    }

    if (WifiBackplaneWrite32(WIFI_SDIO_INT_HOST_MASK, WIFI_HOST_MAILBOX_SOFTWARE_MASK) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI host mask write fail\n");
        return -1;
    }
    if (WifiBackplaneWrite32(WIFI_SDIO_FUNCTION_INT_MASK, 3u) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI func mask write fail\n");
        return -1;
    }
    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_FUNCTION2_WATERMARK,
                               (UINT8)WIFI_FUNCTION2_WATERMARK_VALUE) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI F2 watermark fail\n");
        return -1;
    }
    if (WifiSdioEnableFunction2() != 0) {
        return -1;
    }
    for (Attempt = 0; Attempt < 1000u; Attempt++) {
        if (WifiSdioCommand52Read(SDIO_FUNCTION_0, SDIO_CCCR_IO_READY, &Ready) != 0) {
            HalSerialBootMarkForce("Boot: Pi WIFI F2 ready CMD52 fail\n");
            return -1;
        }
        if ((Ready & (1u << SDIO_FUNCTION_2)) != 0) {
            WifiBootMarkHexadecimal32("Boot: Pi WIFI F2 ready iordy=", Ready);
            HalSerialBootMarkForce("\n");
            WifiBackplaneWindowInvalidate();
            if (WifiBackplaneRead32(WIFI_CHIPCOMMON_BASE, &ChipCommon) == 0) {
                WifiBootMarkHexadecimal32("Boot: Pi WIFI cc after HT=", ChipCommon);
                HalSerialBootMarkForce("\n");
            } else {
                HalSerialBootMarkForce("Boot: Pi WIFI CMD53 read still dead after HT\n");
            }
            return 0;
        }
        SdhciDelayMicroseconds(1000);
    }
    HalSerialBootMarkForce("Boot: Pi WIFI F2 timeout\n");
    return -1;
}
