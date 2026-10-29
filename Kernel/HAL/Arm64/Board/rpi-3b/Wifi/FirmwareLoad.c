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
    const UINT8 Requested = (UINT8)(WIFI_CLOCK_NO_HARDWARE_REQ | WIFI_CLOCK_REQUEST_ALP);
    UINT8 Status = 0;
    UINT32 Attempt;

    /* 对表 brcmf_sdio_buscoreprep：先写 0x28（关硬件时钟请求 + 要 ALP） */
    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL, Requested) != 0) {
        return -1;
    }
    if (WifiSdioCommand52Read(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL, &Status) != 0) {
        return -1;
    }
    /* 源码要求：回读去掉两个 AVAIL 位后必须与写入值逐位相等 */
    if ((UINT8)(Status & (UINT8)~(WIFI_CLOCK_ALP_AVAILABLE | WIFI_CLOCK_HT_AVAILABLE)) !=
        Requested) {
        WifiBootMarkHexadecimal32("Boot: Pi WIFI csr wrote=28 read=", Status);
        HalSerialBootMarkForce("\n");
        return -1;
    }
    /* PMU_MAX_TRANSITION_DLY：ALP 起来最多 15 ms */
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
    /* buscoreprep 末尾：0x21 = FORCE_HW_CLKREQ_OFF | FORCE_ALP */
    if (WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL,
                               (UINT8)(WIFI_CLOCK_NO_HARDWARE_REQ | WIFI_CLOCK_FORCE_ALP)) != 0) {
        return -1;
    }
    SdhciDelayMicroseconds(65);
    /* 关掉多余的 SDIO 上拉 */
    (void)WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_PULL_UP_CONTROL, 0);
    /*
     * 对表 brcmf_sdio_htclk(on)：download_firmware 之前会再调一次 clkctl(CLK_AVAIL)，
     * 那里是整字节覆写成 ALP_AVAIL_REQ —— 会把 FORCE_ALP 和 FORCE_HW_CLKREQ_OFF 一起清掉，
     * 从而放开芯片的硬件时钟申请。我们之前停在 0x21，等于把时钟申请这条路掐死了。
     */
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
    WifiBootMarkHexadecimal32("Boot: Pi WIFI csr=", Status);
    HalSerialBootMarkForce("\n");
    return 0;
}

static int WifiEnterDownload(void) {
    /* 对表 brcmf_chip_cm3_set_passive：禁 ARM → D11 带 PHY 位复位 → SOCRAM 复位 → 关 bank 3 重映射 */
    if (WifiCoreDisable(WIFI_ARM_WRAPPER, 0, 0) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI ARM disable fail\n");
        return -1;
    }
    /* A/B：resetcore 会清 RESET_CTL、把 CM3 放出来从地址 0 取指，这里重新按回去 */
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
    /* A/B 已证：不复位时 rst=1、io=0、连寄存器都写不进 */
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

int WifiFirmwareLoad(void) {
    const char *FirmwarePath;
    const char *NvramPath;
    UINT32 DownloadBase;
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

    WifiBootMarkHexadecimal32("Boot: Pi WIFI load begin tag=", WIFI_BUILD_TAG);
    HalSerialBootMarkForce("\n");
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
    /* 对表 brcmf_sdiod_skbuff_write：长度向上取 4，尾巴清零（避免 2B 收尾） */
    while ((FirmwareBytes & 3u) != 0u && FirmwareBytes < WIFI_FIRMWARE_MAX_BYTES) {
        FirmwareBlob[FirmwareBytes++] = 0;
    }
    WifiDiagnosticsClockState();
    WifiDiagnosticsSocramState();
    DownloadBase = WifiSramFindAccessibleBase();
    if (DownloadBase == 0xFFFFFFFFu) {
        HalSerialBootMarkForce("Boot: Pi WIFI no writable sram window\n");
        goto Done;
    }
    WifiBootMarkHexadecimal32("Boot: Pi WIFI ram base=", DownloadBase);
    HalSerialBootMarkForce("\n");
    if (WifiBackplaneWriteMemory(DownloadBase, FirmwareBlob, (UINT32)FirmwareBytes) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI fw write fail\n");
        goto Done;
    }
    if (WifiVerifyDownloadPath() != 0) {
        goto Done;
    }
    if (WifiBackplaneWriteMemory(DownloadBase + WIFI_RAM_SIZE - 4u - NvramBytes,
                             NvramBlob, NvramBytes) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI nvram write fail\n");
        goto Done;
    }
    WordCount = NvramBytes / 4u;
    SizeWord = ((~WordCount & 0xFFFFu) << 16) | (WordCount & 0xFFFFu);
    if (WifiBackplaneWrite32(DownloadBase + WIFI_RAM_SIZE - 4u, SizeWord) != 0) {
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
