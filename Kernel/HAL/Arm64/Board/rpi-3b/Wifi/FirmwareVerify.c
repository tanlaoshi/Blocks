/*
 * FirmwareVerify.c — 灌片后确认下载通路仍活（pwifi-2）
 *
 * 对表 Linux brcmfmac：brcmf_sdio_verifymemory() 只在 DEBUG 构建里回读，
 * 量产路径灌完直接 set_active。实证本板 bulk 后 CMD53 读 RAM 会楔死且
 * 不可恢复（abort 只能救主机，卡侧背板事务留在队列里），故不再回读。
 */
#include "Wifi/Private.h"

int WifiVerifyDownloadPath(void) {
    UINT8 ClockStatus = 0;

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
    /* 0x49 = FORCE_ALP|ALP_REQ|ALP_AVAIL：写路径与 ALP 都还活着 */
    WifiBootMarkHexadecimal32("Boot: Pi WIFI csr after write=", ClockStatus);
    HalSerialBootMarkForce("\n");
    return 0;
}
