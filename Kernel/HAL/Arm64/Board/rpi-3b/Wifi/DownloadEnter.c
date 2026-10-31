/*
 * DownloadEnter.c — ALP / buscoreprep + cm3_set_passive 进下载态
 *
 * 真机 0x127：AlpRequest 末尾清成 REQUEST_ALP（csr≈0x48）后，bankidx 写
 * 立刻 `dat0 busy over 3000ms`。对表 brcmf_sdio_buscoreprep 应收在
 * 0x21（FORCE_HW_CLKREQ_OFF|FORCE_ALP）；清 FORCE 只在整包灌片前做
 * （对表 download_firmware 里 clkctl(CLK_AVAIL)）。
 */
#include "Wifi/Private.h"

int WifiAlpRequest(void) {
    const UINT8 Requested = (UINT8)(WIFI_CLOCK_NO_HARDWARE_REQ | WIFI_CLOCK_REQUEST_ALP);
    const UINT8 Forced =
        (UINT8)(WIFI_CLOCK_NO_HARDWARE_REQ | WIFI_CLOCK_FORCE_ALP);
    UINT8 Status = 0;
    UINT32 Attempt;

    /* 对表 brcmf_sdio_buscoreprep：先写 0x28（关硬件时钟请求 + 要 ALP） */
    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL, Requested) != 0) {
        return -1;
    }
    if (WifiSdioCommand52Read(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL, &Status) != 0) {
        return -1;
    }
    if ((UINT8)(Status & (UINT8)~(WIFI_CLOCK_ALP_AVAILABLE | WIFI_CLOCK_HT_AVAILABLE)) !=
        Requested) {
        WifiBootMarkHexadecimal32("Boot: Pi WIFI csr wrote=28 read=", Status);
        HalSerialBootMarkForce("\n");
        return -1;
    }
    for (Attempt = 0; Attempt < 15u; Attempt++) {
        if (WifiSdioCommand52Read(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL, &Status) != 0) {
            return -1;
        }
        if ((Status & WIFI_CLOCK_ALP_AVAILABLE) != 0) {
            break;
        }
        SdhciDelayMicroseconds(1000);
    }
    if ((Status & WIFI_CLOCK_ALP_AVAILABLE) == 0) {
        WifiBootMarkHexadecimal32("Boot: Pi WIFI ALP timeout csr=", Status);
        HalSerialBootMarkForce("\n");
        return -1;
    }
    /* buscoreprep 末尾：0x21；寄存器/进下载态期间保持 FORCE_ALP */
    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL, Forced) != 0) {
        return -1;
    }
    SdhciDelayMicroseconds(65);
    (void)WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_PULL_UP_CONTROL, 0);
    if (WifiSdioCommand52Read(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL, &Status) != 0) {
        return -1;
    }
    WifiBootMarkHexadecimal32("Boot: Pi WIFI csr=", Status);
    HalSerialBootMarkForce("\n");
    return 0;
}

/*
 * 对表 download_firmware 前 clkctl(CLK_AVAIL)：整字节写成 ALP_AVAIL_REQ，
 * 清掉 FORCE_*，放开硬件时钟申请。仅在 bankidx / EnterDownload 之后调用。
 */
int WifiAlpReleaseForceToRequest(void) {
    UINT8 Status = 0;
    UINT32 Attempt;

    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL,
                               WIFI_CLOCK_REQUEST_ALP) != 0) {
        return -1;
    }
    for (Attempt = 0; Attempt < 1500u; Attempt++) {
        if (WifiSdioCommand52Read(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL, &Status) != 0) {
            return -1;
        }
        if ((Status & WIFI_CLOCK_ALP_AVAILABLE) != 0) {
            break;
        }
        SdhciDelayMicroseconds(1000);
    }
    WifiBootMarkHexadecimal32("Boot: Pi WIFI csr avail=", Status);
    HalSerialBootMarkForce("\n");
    return ((Status & WIFI_CLOCK_ALP_AVAILABLE) != 0) ? 0 : -1;
}

int WifiEnterDownload(void) {
    /* 对表 brcmf_chip_cm3_set_passive：禁 ARM → D11 带 PHY 位复位 → SOCRAM 复位 → 关 bank 3 重映射 */
    if (WifiCoreDisable(WIFI_ARM_WRAPPER, 0, 0) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI ARM disable fail\n");
        return -1;
    }
    if (WifiBackplaneWrite32(WIFI_ARM_WRAPPER + WIFI_AI_RESET_CONTROL_OFFSET, 1u) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI ARM hold fail\n");
        return -1;
    }
    HalSerialBootMarkForce("Boot: Pi WIFI ARM held in reset\n");
    if (WifiCoreReset(WIFI_D11_WRAPPER, WIFI_D11_PHY_RESET | WIFI_D11_PHY_CLOCK_ENABLE,
                      WIFI_D11_PHY_CLOCK_ENABLE, WIFI_D11_PHY_CLOCK_ENABLE) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI D11 reset fail\n");
        return -1;
    }
    if (WifiCoreReset(WIFI_SOCRAM_WRAPPER, 0, 0, 0) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI SOCRAM up fail\n");
        return -1;
    }
    {
        UINT32 IoControl = 0;
        UINT32 ResetControl = 0;
        UINT32 CoreInfo = 0;
        UINT32 BankInfo = 0;

        (void)WifiBackplaneRead32(WIFI_SOCRAM_WRAPPER + WIFI_AI_IO_CONTROL_OFFSET, &IoControl);
        (void)WifiBackplaneRead32(WIFI_SOCRAM_WRAPPER + WIFI_AI_RESET_CONTROL_OFFSET,
                                  &ResetControl);
        (void)WifiBackplaneRead32(WIFI_SOCRAM_CORE_BASE, &CoreInfo);
        (void)WifiBackplaneRead32(WIFI_SOCRAM_CORE_BASE + 0x40u, &BankInfo);
        WifiBootMarkHexadecimal32("Boot: Pi WIFI socram io=", IoControl);
        WifiBootMarkHexadecimal32(" rst=", ResetControl);
        WifiBootMarkHexadecimal32(" core=", CoreInfo);
        WifiBootMarkHexadecimal32(" bank=", BankInfo);
        HalSerialBootMarkForce("\n");
    }
    if (WifiBackplaneWrite32(WIFI_SOCRAM_BANK_INDEX, 3u) != 0 ||
        WifiBackplaneWrite32(WIFI_SOCRAM_BANK_PDA, 0u) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI bank remap off fail\n");
        return -1;
    }
    HalSerialBootMarkForce("Boot: Pi WIFI SOCRAM up ok\n");
    return 0;
}
