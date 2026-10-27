/*
 * Sdhci.c — Duo S µSD SDHCI 门面（bd-duo-sd）
 */
#include "Sdhci/Private.h"

UINT16 gSdhciHostSpecification;
UINT32 gSdhciVendorArea;
UINT32 gSdhciRelativeCardAddress;
UINT32 gSdhciResponse[4];
UINT32 gSdhciLastStatus;
UINT32 gSdhciLastPresentState;
int gSdhciHighCapacity;
int gSdhciReady;

static int SdhciProbe(UINT32 Drive) {
    return (Drive == 0 && gSdhciReady) ? 1 : 0;
}

static int SdhciRead(UINT32 Drive, UINT32 LogicalBlockAddress, UINT32 Count, void *Buffer) {
    UINT8 *P = (UINT8 *)Buffer;
    UINT32 i;

    (void)Drive;
    if (!gSdhciReady || Buffer == 0 || Count == 0) {
        return 0;
    }
    for (i = 0; i < Count; i++) {
        if (SdhciReadOneBlock(LogicalBlockAddress + i, P + (UINTN)i * 512u) != 0) {
            (void)SdhciSoftwareReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
            return 0;
        }
    }
    return 1;
}

static int SdhciWrite(UINT32 Drive, UINT32 LogicalBlockAddress, UINT32 Count, const void *Buffer) {
    const UINT8 *P = (const UINT8 *)Buffer;
    UINT32 i;

    (void)Drive;
    if (!gSdhciReady || Buffer == 0 || Count == 0) {
        return 0;
    }
    for (i = 0; i < Count; i++) {
        if (SdhciWriteOneBlock(LogicalBlockAddress + i, P + (UINTN)i * 512u) != 0) {
            (void)SdhciSoftwareReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
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
    SdhciMark(gSdhciHighCapacity ? "Boot: SD SDHC ready\n" : "Boot: SD SDSC ready\n");
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

static const DRIVER gSdhciDriver = {
    .Name = "cv1800-sdhci",
    .Class = DRIVER_CLASS_BLOCK,
    .Match = 0,
    .Probe = SdhciDriverProbe,
    .Bind = SdhciDriverBind,
    .Remove = SdhciDriverRemove,
};

void BoardSdhciRegister(void) {
    (void)DriverRegister(&gSdhciDriver);
}
