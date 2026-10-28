/*
 * FirmwareHtBringUp.c — 拉 ARM 后等 HT / 开 F2（pwifi-2）
 *
 * 实证：bulk 后、ARM/HT 前 CMD53 读会楔死；此文件只 CMD52 + Write32。
 */
#include "Wifi/Private.h"

int WifiExitDownload(void) {
    /*
     * 对表 Linux cm3_set_active：activate 清 intstatus → resetcore(ARM)。
     * 只写（bulk 后 CMD53 读楔死）。拉起后延时 + CMD|DATA 复位再轮询 HT。
     */
    if (WifiBackplaneWrite32(WIFI_SDIO_INT_STATUS, 0xFFFFFFFFu) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI intstatus clear fail\n");
        return -1;
    }
    if (WifiCoreResetWritesOnly(WIFI_ARM_WRAPPER, 0, 0, 0) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI ARM reset fail\n");
        return -1;
    }
    HalSerialBootMarkForce("Boot: Pi WIFI ARM reset (writes-only)\n");
    SdhciDelayMicroseconds(50000);
    if (WifiSdioHostCommandDataRecover() != 0) {
        return -1;
    }
    HalSerialBootMarkForce("Boot: Pi WIFI HT wait begin\n");
    return 0;
}

int WifiWaitHtAndFunction2(void) {
    UINT32 Attempt;
    UINT8 Ready = 0;
    UINT8 Status = 0;
    UINT32 ChipCommon = 0;

    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL,
                               (UINT8)WIFI_CLOCK_REQUEST_HT) != 0) {
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
            (void)WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL,
                                         (UINT8)WIFI_CLOCK_REQUEST_HT);
        }
        SdhciDelayMicroseconds(1000);
    }
    if ((Status & WIFI_CLOCK_HT_AVAILABLE) == 0) {
        WifiBootMarkHexadecimal32("Boot: Pi WIFI HT timeout csr=", Status);
        HalSerialBootMarkForce("\n");
        return -1;
    }
    WifiBootMarkHexadecimal32("Boot: Pi WIFI HT ok csr=", Status);
    HalSerialBootMarkForce("\n");

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
