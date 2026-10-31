/*
 * FirmwareVerify.c — 灌片后通路探活 + 轻量抽样读回（pi-wifi-4）
 *
 * 对表 Linux brcmfmac：brcmf_sdio_verifymemory() 只在 DEBUG 构建里回读，
 * 量产路径灌完直接 set_active。实证本板 bulk CMD53 读 RAM 会楔死且
 * 不可恢复（abort 只能救主机），故只做若干 dword 的 Read32 抽样。
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

/*
 * 同周期抽前若干 dword，对表镜像；禁止 bulk ReadMemory。
 * 任一字读失败 / 与镜像不符 / 全 0 → 失败。
 */
int WifiFirmwareSampleReadback(UINT32 DownloadBase, const UINT8 *FirmwareBlob,
                               UINT32 FirmwareBytes) {
    UINT32 SampleWords = 4u;
    UINT32 WordIndex;
    UINT32 Expected;
    UINT32 Got;
    UINT32 NonZero = 0;

    if (FirmwareBlob == 0 || FirmwareBytes < 4u) {
        return -1;
    }
    if (FirmwareBytes < SampleWords * 4u) {
        SampleWords = FirmwareBytes / 4u;
    }
    for (WordIndex = 0; WordIndex < SampleWords; WordIndex++) {
        Expected = (UINT32)FirmwareBlob[WordIndex * 4u] |
                   ((UINT32)FirmwareBlob[WordIndex * 4u + 1u] << 8) |
                   ((UINT32)FirmwareBlob[WordIndex * 4u + 2u] << 16) |
                   ((UINT32)FirmwareBlob[WordIndex * 4u + 3u] << 24);
        Got = 0;
        if (WifiBackplaneRead32(DownloadBase + WordIndex * 4u, &Got) != 0) {
            WifiBootMarkHexadecimal32("Boot: Pi WIFI sample read fail @",
                                      DownloadBase + WordIndex * 4u);
            HalSerialBootMarkForce("\n");
            return -1;
        }
        WifiBootMarkHexadecimal32("Boot: Pi WIFI sample@", DownloadBase + WordIndex * 4u);
        WifiBootMarkHexadecimal32(" got=", Got);
        WifiBootMarkHexadecimal32(" want=", Expected);
        HalSerialBootMarkForce("\n");
        if (Got != Expected) {
            HalSerialBootMarkForce("Boot: Pi WIFI sample mismatch\n");
            return -1;
        }
        if (Got != 0u) {
            NonZero = 1;
        }
    }
    if (NonZero == 0u) {
        HalSerialBootMarkForce("Boot: Pi WIFI sample all zero\n");
        return -1;
    }
    HalSerialBootMarkForce("Boot: Pi WIFI sample ok\n");
    return 0;
}
