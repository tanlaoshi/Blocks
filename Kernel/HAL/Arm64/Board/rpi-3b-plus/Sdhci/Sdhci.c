/*
 * Sdhci.c — Pi3B+ Arasan SDHCI 门面（bd-pi-sd；WiFi SDIO，非 µSD）
 */
#include "Sdhci/Private.h"

UINT16 gSdhciHostSpecification;
UINT32 gSdhciLastCommandStatus;
UINT32 gSdhciLastPresentState;
static int SdhciProbe(UINT32 Drive) {
    return (Drive == 0 && gSdhciReady) ? 1 : 0;
}

static int SdhciRead(UINT32 Drive, UINT32 LogicalBlockAddress, UINT32 Count, void *Buffer) {
    UINT8 *P = (UINT8 *)Buffer;
    UINT32 i;

    (void)Drive;
    if (!gSdhciReady || !Buffer || Count == 0) {
        return 0;
    }
    for (i = 0; i < Count; i++) {
        if (SdhciReadOneBlock(LogicalBlockAddress + i, P + (UINTN)i * 512u) != 0) {
            SdhciSoftwareReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
            return 0;
        }
    }
    return 1;
}

static int SdhciWrite(UINT32 Drive, UINT32 LogicalBlockAddress, UINT32 Count, const void *Buffer) {
    (void)Drive;
    (void)LogicalBlockAddress;
    (void)Count;
    (void)Buffer;
    return 0; /* 本刀只读 */
}

const BLOCK_BACKEND gSdhciBackend = {
    .Probe = SdhciProbe,
    .ReadSectors = SdhciRead,
    .WriteSectors = SdhciWrite,
    .Flush = 0,
};

static int SdhciDriverProbe(const DRIVER *Self, void *BusContext, void **OutPrivate) {
    (void)Self;
    (void)BusContext;
    if (gSdhciReady) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    if (SdhciHostInitialize() != 0) {
        return -1;
    }
    gSdhciReady = 1;
    HalSerialBootMark(gSdhciHighCapacity ? "Boot: SDHCI SDHC ready\n" : "Boot: SDHCI SDSC ready\n");
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int SdhciDriverBind(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    return DriverBlockAttach(&gSdhciBackend);
}

static void SdhciDriverRemove(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gSdhciReady = 0;
}

const DRIVER gSdhciDriver = {
    .Name = "bcm-sdhci",
    .Class = DRIVER_CLASS_BLOCK,
    .Match = 0,
    .Probe = SdhciDriverProbe,
    .Bind = SdhciDriverBind,
    .Remove = SdhciDriverRemove,
};

void BoardSdhciRegister(void) {
    /* 故意不挂：µSD 用 BoardSdhostRegister；本驱动留档勿再 Probe 读卡 */
    (void)gSdhciDriver;
    HalSerialBootMark("Boot: SDHCI skipped (WiFi SDIO, not µSD)\n");
}
