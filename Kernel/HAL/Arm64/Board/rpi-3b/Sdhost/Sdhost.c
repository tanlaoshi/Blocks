/*
 * Sdhost.c — Pi3B µSD SDHOST 门面（bd-pi-sd）
 */
#include "Sdhost/Private.h"
#include "FileSystem.h"
#include "HalSerial.h"

UINT32 gSdhostMaximumClock;
UINT32 gSdhostHostConfiguration;
UINT32 gSdhostClockDivider;
UINT32 gSdhostRelativeCardAddress;
UINT32 gSdhostResponse[4];
int gSdhostHighCapacity;
int gSdhostReady;
int gSdhostIoBusy;
int gSdhostCardLost;
UINT32 gSdhostLastCommand;
UINT32 gSdhostLastHostStatus;
UINT64 gSdhostCounterFrequency;

static int SdhostProbe(UINT32 Drive) {
    return (Drive == 0 && gSdhostReady) ? 1 : 0;
}

static int SdhostRead(UINT32 Drive, UINT32 LogicalBlockAddress, UINT32 Count, void *Buffer) {
    UINT8 *P = (UINT8 *)Buffer;
    UINT32 i;

    (void)Drive;
    if (!Buffer || Count == 0) {
        return 0;
    }
    if (!gSdhostReady) {
        if (gSdhostCardLost) {
            FileSystemNotifyBootWatchUnplug();
        }
        return 0;
    }
    gSdhostIoBusy++;
    for (i = 0; i < Count; i++) {
        if (SdhostReadOneBlock(LogicalBlockAddress + i, P + (UINTN)i * 512u) != 0) {
            (void)SdhostWaitTransferComplete();
            gSdhostCardLost = 1;
            gSdhostReady = 0;
            gSdhostIoBusy--;
            HalSerialWriteShell("\nFS: sdhost read fail → unplug\n");
            FileSystemNotifyBootWatchUnplug();
            return 0;
        }
    }
    gSdhostIoBusy--;
    return 1;
}

static int SdhostWrite(UINT32 Drive, UINT32 LogicalBlockAddress, UINT32 Count, const void *Buffer) {
    const UINT8 *P = (const UINT8 *)Buffer;
    UINT32 i;

    (void)Drive;
    if (Buffer == 0 || Count == 0) {
        return 0;
    }
    if (!gSdhostReady) {
        if (gSdhostCardLost) {
            FileSystemNotifyBootWatchUnplug();
        }
        return 0;
    }
    gSdhostIoBusy++;
    for (i = 0; i < Count; i++) {
        if (SdhostWriteOneBlock(LogicalBlockAddress + i, P + (UINTN)i * 512u) != 0) {
            (void)SdhostWaitTransferComplete();
            gSdhostCardLost = 1;
            gSdhostReady = 0;
            gSdhostIoBusy--;
            HalSerialWriteShell("\nFS: sdhost write fail → unplug\n");
            FileSystemNotifyBootWatchUnplug();
            return 0;
        }
    }
    gSdhostIoBusy--;
    return 1;
}

const BLOCK_BACKEND gSdhostBackend = {
    .Probe = SdhostProbe,
    .ReadSectors = SdhostRead,
    .WriteSectors = SdhostWrite,
    .Flush = 0,
};

static int SdhostDriverProbe(const DRIVER *Self, void *BusContext, void **OutPrivate) {
    (void)Self;
    (void)BusContext;
    if (gSdhostReady) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    if (SdhostHostInitialize() != 0) {
        return -1;
    }
    gSdhostReady = 1;
    HalSerialBootMark(gSdhostHighCapacity ? "Boot: SDHOST SDHC ready\n" : "Boot: SDHOST SDSC ready\n");
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int SdhostDriverBind(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    return DriverBlockAttach(&gSdhostBackend);
}

static void SdhostDriverRemove(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gSdhostReady = 0;
}

const DRIVER gSdhostDriver = {
    .Name = "bcm-sdhost",
    .Class = DRIVER_CLASS_BLOCK,
    .Match = 0,
    .Probe = SdhostDriverProbe,
    .Bind = SdhostDriverBind,
    .Remove = SdhostDriverRemove,
};

void BoardSdhostRegister(void) {
    (void)DriverRegister(&gSdhostDriver);
}
