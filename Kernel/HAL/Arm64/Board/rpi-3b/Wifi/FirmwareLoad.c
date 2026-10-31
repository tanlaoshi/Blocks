/*
 * FirmwareLoad.c — 灌 brcmfmac bin + NVRAM（pi-wifi-4）
 * HT/F2 见 FirmwareHtBringUp.c（pi-wifi-5）。对表 Linux brcmf_sdio_download_firmware。
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
    /*
     * 延后 Claim：空闲后 KSO 写得成 0x01 但 DEVON 不回（0x12c）。
     * 先 WL_ON POR + 重枚举，再对表 Linux 走 KSO。
     */
    if (WifiSdioBusRejoinAfterIdle() != 0) {
        goto Done;
    }
    /* Linux：SLEEPCSR KSO → 再 CHIPCLK / 背板窗；见 SleepControl.c */
    if (WifiSdioSleepControlEnsureAwake() != 0) {
        goto Done;
    }
    if (WifiAlpRequest() != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI ALP fail\n");
        goto Done;
    }
    (void)WifiSdioHostCommandDataRecover();
    /*
     * 0x128：在 SOCRAM 未复位前写 bankidx → DAT0 楔死（与 FORCE 无关）。
     * 进下载前只读 ChipCommon 探活；bankidx 写读回放到 EnterDownload 之后。
     */
    {
        UINT32 ChipCommon = 0;

        if (WifiBackplaneRead32(WIFI_CHIPCOMMON_BASE, &ChipCommon) != 0) {
            HalSerialBootMarkForce("Boot: Pi WIFI cc read fail before download\n");
            goto Done;
        }
        WifiBootMarkHexadecimal32("Boot: Pi WIFI cc before download=", ChipCommon);
        HalSerialBootMarkForce("\n");
    }
    if (WifiEnterDownload() != 0) {
        goto Done;
    }
    /*
     * 0x139：min←ffffffff → res≈7fffffff，仍 DTO。
     * 0x13a：资源拉齐后再 SOCRAM CoreReset+bankpda（复位时阵列可能尚未上电）。
     */
    if (WifiSdioSetupResetBehavior() != 0) {
        goto Done;
    }
    /*
     * 0x13b CH：路径同 0x13a（含探针前已 csr avail=48）仍 DTO。
     * DownloadEnter：寄存器/进下载态保持 FORCE；清 FORCE 只在整包灌片前。
     * 0x12f 否的是「无 PMU 再复位、只靠 FORCE」——此处 FORCE 贯穿 PMU+再复位+探针。
     */
    if (WifiPowerManagementUnitForceResources() != 0) {
        goto Done;
    }
    if (WifiSocramResetAfterResources() != 0) {
        goto Done;
    }
    if (WifiBackplaneSramProbeWrite() != 0) {
        goto Done;
    }
    if (WifiAlpReleaseForceToRequest() != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI ALP avail fail\n");
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
    WifiBootMarkHexadecimal32("Boot: Pi WIFI fw bytes=", (UINT32)FirmwareBytes);
    HalSerialBootMarkForce("\n");
    if (WifiVerifyDownloadPath() != 0) {
        goto Done;
    }
    if (WifiFirmwareSampleReadback(DownloadBase, FirmwareBlob, (UINT32)FirmwareBytes) != 0) {
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
    /*
     * pi-wifi-4：镜像 + NVRAM + 抽样绿即可。
     * ExitDownload / HT / F2 留给 pi-wifi-5；勿在此宣称 gWifiFirmwareLoaded。
     */
    HalSerialBootMarkForce("Boot: Pi WIFI nvram ok, wifi-4 stop before exit download\n");
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
