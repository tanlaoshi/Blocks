/*
 * Wifi.c — Pi3B WiFi 门面：探针 + 灌片 + HalWifi ops（pwifi-1/2）
 */
#include "Wifi/Private.h"
#include "HalDevices.h"

static int WifiOpsClaim(void) {
    if (!gWifiChipProbed) {
        return 0;
    }
    if (gWifiFirmwareLoaded) {
        return 1;
    }
    WifiFirmwarePathProbe();
    if (!gWifiFirmwarePresent) {
        return 1; /* 有芯；固件后补 */
    }
    if (WifiFirmwareLoad() != 0) {
        return 1;
    }
    (void)WifiRadioBringUp();
    return 1;
}

static int WifiOpsReady(void) {
    return (gWifiChipProbed && gWifiFirmwareLoaded) ? 1 : 0;
}

int BoardWifiClaim(void) {
    return WifiOpsClaim();
}

int BoardWifiReady(void) {
    return WifiOpsReady();
}

int BoardWifiScan(void) {
    return WifiScanAccessPoints();
}

int BoardWifiAssociateOpen(const char *Ssid) {
    return WifiAssociateOpen(Ssid);
}

UINT32 BoardWifiScanEntryCount(void) {
    return gWifiScanEntryCount;
}

const BOARD_WIFI_SCAN_ENTRY *BoardWifiScanEntryAt(UINT32 Index) {
    if (Index >= gWifiScanEntryCount) {
        return 0;
    }
    return &gWifiScanEntries[Index];
}

static int WifiOpsScan(void) {
    return WifiScanAccessPoints();
}

static UINT32 WifiOpsScanCount(void) {
    return gWifiScanEntryCount;
}

static int WifiOpsScanEntryAt(UINT32 Index, char *SsidOut, UINT32 SsidCapacity, INT32 *RssiOut,
                              UINT16 *ChannelOut) {
    const BOARD_WIFI_SCAN_ENTRY *Entry;
    UINT32 Copy;
    UINT32 ByteIndex;

    Entry = BoardWifiScanEntryAt(Index);
    if (Entry == 0 || SsidOut == 0 || SsidCapacity == 0) {
        return -1;
    }
    Copy = Entry->SsidLength;
    if (Copy >= SsidCapacity) {
        Copy = SsidCapacity - 1u;
    }
    for (ByteIndex = 0; ByteIndex < Copy; ByteIndex++) {
        SsidOut[ByteIndex] = Entry->Ssid[ByteIndex];
    }
    SsidOut[Copy] = 0;
    if (RssiOut != 0) {
        *RssiOut = Entry->SignalRssi;
    }
    if (ChannelOut != 0) {
        *ChannelOut = Entry->Channel;
    }
    return 0;
}

static int WifiOpsAssociateOpen(const char *Ssid) {
    return WifiAssociateOpen(Ssid);
}

static int WifiProbeOnce(void) {
    WifiPinConfigure();
    if (WifiWirelessPowerOn() != 0) {
        return -1;
    }
    if (WifiSdioHostBringUp() != 0) {
        return -1;
    }
    if (WifiSdioEnumerate() != 0) {
        return -1;
    }
    if (WifiSdioEnableFunction1() != 0) {
        return -1;
    }
    if (WifiChipIdentityProbe() != 0) {
        return -1;
    }
    WifiBringUpDiagnosticsPrint();
    {
        const char *Path = WifiFirmwarePathForChip();
        HalSerialBootMark("Boot: Pi WIFI fw path=");
        HalSerialBootMark(Path ? Path : "(none)");
        HalSerialBootMark(" (load after FS)\n");
    }
    return 0;
}

void BoardWifiRegister(void) {
    static const HAL_WIFI_OPS Ops = {
        .Claim = WifiOpsClaim,
        .Ready = WifiOpsReady,
        .Scan = WifiOpsScan,
        .ScanCount = WifiOpsScanCount,
        .ScanEntryAt = WifiOpsScanEntryAt,
        .AssociateOpen = WifiOpsAssociateOpen,
    };

    HalWifiSetOps(&Ops);
    if (WifiProbeOnce() != 0) {
        HalSerialBootMark("Boot: Pi WIFI probe fail (Ready=0)\n");
        gWifiChipProbed = 0;
        return;
    }
    HalSerialBootMark("Boot: Pi WIFI ok\n");
}
