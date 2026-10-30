/*
 * Sdhci.c — Duo S SDHCI 门面（bd-duo-sd）
 *
 * PR-30 S1：两个控制器实例（SD0 卡槽 / SD1 wifi-sd）共用一份驱动代码；
 * 块设备只暴露 SD0，SD1 在同一个 Probe 里做 SDIO 枚举（失败不影响启动盘）。
 */
#include "Sdhci/Private.h"

static SDHCI_HOST gSdhciHosts[SDHCI_HOST_COUNT] = {
    {.Base = SDHCI_BASE_SD0, .Index = 0, .BusWidth = 1, .CardType = SDHCI_CARD_NONE},
    {.Base = SDHCI_BASE_SD1,
     .Index = 1,
     .BusWidth = 1,
     .CardType = SDHCI_CARD_NONE,
     .NonRemovable = 1},
};

SDHCI_HOST *SdhciHostAt(UINT32 Index) {
    return (Index < SDHCI_HOST_COUNT) ? &gSdhciHosts[Index] : 0;
}

static int SdhciProbe(UINT32 Drive) {
    return (Drive == 0 && gSdhciHosts[0].Ready) ? 1 : 0;
}

static int SdhciRead(UINT32 Drive, UINT32 LogicalBlockAddress, UINT32 Count, void *Buffer) {
    UINT8 *P = (UINT8 *)Buffer;
    UINT32 i;

    (void)Drive;
    if (!gSdhciHosts[0].Ready || Buffer == 0 || Count == 0) {
        return 0;
    }
    for (i = 0; i < Count; i++) {
        if (SdhciReadOneBlock(&gSdhciHosts[0], LogicalBlockAddress + i, P + (UINTN)i * 512u) !=
            0) {
            (void)SdhciSoftwareReset(&gSdhciHosts[0], SDHCI_RESET_CMD | SDHCI_RESET_DATA);
            return 0;
        }
    }
    return 1;
}

static int SdhciWrite(UINT32 Drive, UINT32 LogicalBlockAddress, UINT32 Count, const void *Buffer) {
    const UINT8 *P = (const UINT8 *)Buffer;
    UINT32 i;

    (void)Drive;
    if (!gSdhciHosts[0].Ready || Buffer == 0 || Count == 0) {
        return 0;
    }
    for (i = 0; i < Count; i++) {
        if (SdhciWriteOneBlock(&gSdhciHosts[0], LogicalBlockAddress + i, P + (UINTN)i * 512u) !=
            0) {
            (void)SdhciSoftwareReset(&gSdhciHosts[0], SDHCI_RESET_CMD | SDHCI_RESET_DATA);
            return 0;
        }
    }
    return 1;
}

static const BLOCK_BACKEND gSdhciBackend = {
    .Probe = SdhciProbe,
    .ReadSectors = SdhciRead,
    .WriteSectors = SdhciWrite,
    .Flush = 0,
};

static int SdhciDriverProbe(const DRIVER *Self, void *BusContext, void **OutPrivate) {
    SDHCI_HOST *Host0 = &gSdhciHosts[0];
    int Rc;

    (void)Self;
    (void)BusContext;
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    SdhciMark("Boot: sdhci probe enter (SD0 boot disk)\n");
    if (Host0->Ready) {
        /*
         * SD0 已经就绪（启动盘早一步绑好了）——这里以前直接 return，
         * 结果 SD1（wifi-sd）的探测在真机上**根本没跑过**，开机日志里一行
         * `WiFi:` 都没有，只有手动 wifiprobe 才会去探。
         * SD0 是否就绪与 SD1 无关，SD1 该探就得探。
         */
        SdhciMark("Boot: sdhci probe skip, SD0 already ready (SD1 still probed)\n");
        (void)SdhciWifiInitialize();
        return 0;
    }
    Rc = SdhciHostInitialize(Host0);
    if (Rc == 0) {
        Host0->Ready = 1;
        SdhciMark(Host0->HighCapacity ? "Boot: SD SDHC ready\n" : "Boot: SD SDSC ready\n");
    }
    /*
     * SD1（wifi-sd）与启动盘互不影响：SD0 成败都要试一次 SD1，
     * SD1 自己的失败只打日志，不改块设备状态（gWifiSdioState 保证只试一次）。
     */
    SdhciMark("Boot: sdhci SD0 rc=");
    SdhciMark(Rc == 0 ? "0\n" : "-1\n");
    (void)SdhciWifiInitialize();
    return Rc;
}

static int SdhciDriverBind(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    return DriverBlockAttach(&gSdhciBackend);
}

static void SdhciDriverRemove(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gSdhciHosts[0].Ready = 0;
}

static const DRIVER gSdhciDriver = {
    .Name = "cv1800-sdhci",
    .Class = DRIVER_CLASS_BLOCK,
    .Match = 0,
    .Probe = SdhciDriverProbe,
    .Bind = SdhciDriverBind,
    .Remove = SdhciDriverRemove,
};

void BoardSdhciRegister(void) {
    /*
     * 构建指纹：真机上先看这一行。没有它 ⇒ 卡上跑的不是这次编的内核；
     * 有它但没有 "Boot: sdhci probe enter" ⇒ BLOCK 类探测没走到这个驱动。
     */
    SdhciMark("Boot: duo-sdhci build=" __DATE__ " " __TIME__ " hosts=2\n");
    (void)DriverRegister(&gSdhciDriver);
}
