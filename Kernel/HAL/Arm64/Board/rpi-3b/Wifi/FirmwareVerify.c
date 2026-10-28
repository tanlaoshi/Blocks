/*
 * FirmwareVerify.c — 灌片后 ALP + 可选 4B 字校验（pwifi-2）
 *
 * 大块 CMD53 读曾楔死；4B Read32 与 Write32 同形，能读则核对 fw[4]。
 */
#include "Wifi/Private.h"

int WifiVerifyFirmwareWord(const UINT8 *Firmware) {
    UINT8 ClockStatus = 0;
    UINT32 WordAtFour = 0;
    UINT32 Expect;

    if (Firmware == 0) {
        return -1;
    }
    Expect = (UINT32)Firmware[4] | ((UINT32)Firmware[5] << 8) | ((UINT32)Firmware[6] << 16) |
             ((UINT32)Firmware[7] << 24);
    if (WifiSdioHostCommandDataRecover() != 0) {
        return -1;
    }
    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL,
                               (UINT8)(WIFI_CLOCK_REQUEST_ALP | WIFI_CLOCK_FORCE_ALP)) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI ALP re-req fail\n");
        return -1;
    }
    SdhciDelayMicroseconds(2000);
    if (WifiSdioCommand52Read(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL, &ClockStatus) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI F1 CMD52 dead after write\n");
        return -1;
    }
    WifiBootMarkHexadecimal32("Boot: Pi WIFI csr after write=", ClockStatus);
    HalSerialBootMarkForce("\n");
    /* 4B Read32（与 Write32 同形）；对上则灌片可信，楔死则 recover 后继续 */
    if (WifiBackplaneRead32(4u, &WordAtFour) == 0) {
        WifiBootMarkHexadecimal32("Boot: Pi WIFI fw[4]=", WordAtFour);
        HalSerialBootMarkForce("\n");
        if (WordAtFour != Expect) {
            WifiBootMarkHexadecimal32("Boot: Pi WIFI fw[4] expect=", Expect);
            HalSerialBootMarkForce("\n");
            HalSerialBootMarkForce("Boot: Pi WIFI fw image mismatch\n");
            return -1;
        }
        HalSerialBootMarkForce("Boot: Pi WIFI fw word ok\n");
    } else {
        HalSerialBootMarkForce("Boot: Pi WIFI fw word read skip\n");
        (void)WifiSdioHostCommandDataRecover();
        (void)WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL,
                                     (UINT8)(WIFI_CLOCK_REQUEST_ALP | WIFI_CLOCK_FORCE_ALP));
    }
    return 0;
}
