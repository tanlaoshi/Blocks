/*
 * FirmwareLoad.c — 灌 brcmfmac bin + NVRAM（pwifi-2）
 * HT/F2 见 FirmwareHtBringUp.c。对表 Linux brcmf_sdio_download_firmware。
 */
#include "Wifi/Private.h"

static int WifiReadFileIntoPages(const char *Path, UINT8 **OutBlob, UINTN *OutBytes,
                                 UINT32 MaxBytes) {
    UINT8 Probe[16];
    UINTN Size = 0;
    UINTN Got = 0;
    UINT32 Pages;
    void *Memory;
    int Error;

    *OutBlob = 0;
    *OutBytes = 0;
    Error = HalDriverFileRead(Path, Probe, sizeof(Probe), &Size);
    if (Error != 0 || Size == 0) {
        return -1;
    }
    Pages = (MaxBytes + PAGE_SIZE - 1u) / PAGE_SIZE;
    Memory = PhysicalMemoryAllocatePages(Pages);
    if (Memory == 0) {
        return -1;
    }
    Error = HalDriverFileRead(Path, Memory, Pages * PAGE_SIZE, &Got);
    if (Error != 0 || Got < 64u || Got > MaxBytes) {
        PhysicalMemoryFreePages(Memory, Pages);
        return -1;
    }
    *OutBlob = (UINT8 *)Memory;
    *OutBytes = Got;
    return 0;
}

static int WifiAlpRequest(void) {
    UINT8 Status = 0;
    UINT32 Attempt;

    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL,
                               (UINT8)(WIFI_CLOCK_REQUEST_ALP | WIFI_CLOCK_FORCE_ALP)) != 0) {
        return -1;
    }
    for (Attempt = 0; Attempt < 50u; Attempt++) {
        if (WifiSdioCommand52Read(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL, &Status) != 0) {
            return -1;
        }
        if ((Status & WIFI_CLOCK_ALP_AVAILABLE) != 0) {
            /* 保持 ALP 请求，灌片期背板要钟（勿清 0） */
            return 0;
        }
        SdhciDelayMicroseconds(1000);
    }
    return -1;
}

static int WifiEnterDownload(void) {
    /*
     * 对表 Linux cm3_set_passive：停 ARM → 复位 D11 → 复位 SOCRAM → 关 bank3 remap。
     * D11 wrap 已钉 0x18101000；失败只打点，不挡灌片（cyw43 可跳 D11）。
     */
    if (WifiCoreDisable(WIFI_ARM_WRAPPER, 0, 0) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI ARM disable fail\n");
        return -1;
    }
    if (WifiCoreReset(WIFI_D11_WRAPPER, WIFI_D11_PHY_RESET | WIFI_D11_PHY_CLOCK_ENABLE,
                      WIFI_D11_PHY_CLOCK_ENABLE, WIFI_D11_PHY_CLOCK_ENABLE) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI D11 reset fail\n");
    } else {
        HalSerialBootMarkForce("Boot: Pi WIFI D11 reset ok\n");
    }
    if (WifiCoreReset(WIFI_SOCRAM_WRAPPER, 0, 0, 0) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI SOCRAM reset fail\n");
        return -1;
    }
    if (WifiBackplaneWrite32(WIFI_SOCRAM_BANK_INDEX, 3u) != 0 ||
        WifiBackplaneWrite32(WIFI_SOCRAM_BANK_PDA, 0) != 0) {
        return -1;
    }
    return 0;
}

int WifiFirmwareLoad(void) {
    const char *FirmwarePath;
    const char *NvramPath;
    UINT8 *FirmwareBlob = 0;
    UINT8 *NvramText = 0;
    UINT8 *NvramBlob = 0;
    UINTN FirmwareBytes = 0;
    UINTN NvramTextBytes = 0;
    UINT32 NvramBytes = 0;
    UINT32 NvramPages;
    UINT32 WordCount;
    UINT32 SizeWord;
    int Result = -1;

    if (!gWifiChipProbed) {
        return -1;
    }
    if (gWifiFirmwareLoaded) {
        return 0;
    }

    FirmwarePath = WifiFirmwarePathForChip();
    NvramPath = WifiNvramPathForChip();
    if (FirmwarePath == 0 || NvramPath == 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI load path unknown\n");
        return -1;
    }
    /* 路径探测只在 Claim 做一次；此处勿再 PathProbe（会双打 fw present） */
    if (!gWifiFirmwarePresent) {
        return -1;
    }
    if (WifiReadFileIntoPages(FirmwarePath, &FirmwareBlob, &FirmwareBytes,
                              WIFI_FIRMWARE_MAX_BYTES) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI fw read fail\n");
        return -1;
    }
    if (WifiReadFileIntoPages(NvramPath, &NvramText, &NvramTextBytes, WIFI_NVRAM_MAX_BYTES) !=
        0) {
        HalSerialBootMarkForce("Boot: Pi WIFI nvram miss\n");
        goto Done;
    }
    NvramPages = (WIFI_NVRAM_MAX_BYTES + PAGE_SIZE - 1u) / PAGE_SIZE;
    NvramBlob = (UINT8 *)PhysicalMemoryAllocatePages(NvramPages);
    if (NvramBlob == 0) {
        goto Done;
    }
    if (WifiNvramPrepare(NvramText, NvramTextBytes, NvramBlob, WIFI_NVRAM_MAX_BYTES,
                         &NvramBytes) != 0 ||
        NvramBytes < 4u) {
        HalSerialBootMarkForce("Boot: Pi WIFI nvram prep fail\n");
        goto Done;
    }

    HalSerialBootMarkForce("Boot: Pi WIFI load begin\n");
    if (WifiAlpRequest() != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI ALP fail\n");
        goto Done;
    }
    if (WifiEnterDownload() != 0) {
        goto Done;
    }
    /* cyw43：寄存器写后退窗到 ChipCommon，再灌 RAM */
    if (WifiBackplaneWindowSet(WIFI_CHIPCOMMON_BASE) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI window home fail\n");
        goto Done;
    }
    {
        UINT32 ChipCommon = 0;

        if (WifiBackplaneRead32(WIFI_CHIPCOMMON_BASE, &ChipCommon) != 0) {
            HalSerialBootMarkForce("Boot: Pi WIFI F1 dead before write\n");
            goto Done;
        }
        WifiBootMarkHexadecimal32("Boot: Pi WIFI cc before write=", ChipCommon);
        HalSerialBootMarkForce("\n");
    }
    if (WifiAlpRequest() != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI ALP re-req fail\n");
        goto Done;
    }
    if (WifiBackplaneWriteMemory(0, FirmwareBlob, (UINT32)FirmwareBytes) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI fw write fail\n");
        goto Done;
    }
    if (WifiVerifyFirmwareWord(FirmwareBlob) != 0) {
        goto Done;
    }
    if (WifiBackplaneWriteMemory(WIFI_RAM_SIZE - 4u - NvramBytes, NvramBlob, NvramBytes) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI nvram write fail\n");
        goto Done;
    }
    WordCount = NvramBytes / 4u;
    SizeWord = ((~WordCount & 0xFFFFu) << 16) | (WordCount & 0xFFFFu);
    if (WifiBackplaneWrite32(WIFI_RAM_SIZE - 4u, SizeWord) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI nvram size write fail\n");
        goto Done;
    }
    HalSerialBootMarkForce("Boot: Pi WIFI nvram ok, exit download\n");
    if (WifiExitDownload() != 0) {
        goto Done;
    }
    if (WifiWaitHtAndFunction2() != 0) {
        goto Done;
    }

    gWifiFirmwareLoaded = 1;
    gWifiSdpcmSequence = 0;
    gWifiSdpcmBusCredit = 1;
    gWifiIoctlRequestIdentity = 0;
    HalSerialBootMarkForce("Boot: Pi WIFI fw load ok\n");
    Result = 0;

Done:
    if (FirmwareBlob != 0) {
        PhysicalMemoryFreePages(
            FirmwareBlob, (WIFI_FIRMWARE_MAX_BYTES + PAGE_SIZE - 1u) / PAGE_SIZE);
    }
    if (NvramText != 0) {
        PhysicalMemoryFreePages(NvramText,
                                (WIFI_NVRAM_MAX_BYTES + PAGE_SIZE - 1u) / PAGE_SIZE);
    }
    if (NvramBlob != 0) {
        PhysicalMemoryFreePages(NvramBlob,
                                (WIFI_NVRAM_MAX_BYTES + PAGE_SIZE - 1u) / PAGE_SIZE);
    }
    return Result;
}
