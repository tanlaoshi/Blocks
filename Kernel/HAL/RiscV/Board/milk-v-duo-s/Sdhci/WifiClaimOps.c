/*
 * WifiClaimOps.c — Duo HalWifi Ops（对表 Pi BoardWifiRegister）
 *
 * Worker→HalWifiClaim：探 SDIO → 灌片 write+confirm → bringup → scan。
 * 不含 join（课密须壳/配置；装钥后 netif 仍自动武装）。
 *
 * BT：本 Claim 灌 fmacfw；其后 HalBluetoothInitialize 可再切 fmacfwbt（Docs/28）。
 */
#include "Sdhci.h"
#include "WifiScanPrivate.h"
#include "Hal.h"
#include "HalDevices.h"

static int sDuoWifiFirmwareLoaded;

static int DuoWifiClaim(void) {
    if (sDuoWifiFirmwareLoaded) {
        return 1;
    }

    if (!SdhciWifiReady()) {
        HalSerialBootMarkForce("Boot: Duo WIFI claim probe\n");
        if (SdhciWifiProbe(0) != 0) {
            HalSerialBootMarkForce("Boot: Duo WIFI claim probe fail\n");
            return 0;
        }
    }

    HalSerialBootMarkForce("Boot: Duo WIFI claim fw write\n");
    if (SdhciWifiFw(SDHCI_WIFI_FW_WRITE | SDHCI_WIFI_FW_CONFIRM) != 0) {
        HalSerialBootMarkForce("Boot: Duo WIFI claim fw fail\n");
        return 1;
    }
    sDuoWifiFirmwareLoaded = 1;
    HalSerialBootMarkForce("Boot: Duo WIFI claim fw ok\n");

    if (SdhciWifiBringup(0) != 0) {
        HalSerialBootMarkForce("Boot: Duo WIFI claim bringup fail\n");
        return 1;
    }
    HalSerialBootMarkForce("Boot: Duo WIFI claim bringup ok\n");

    if (SdhciWifiScan(AIC_SCAN_MODE_RUN, 0) != 0) {
        HalSerialBootMarkForce("Boot: Duo WIFI claim scan fail\n");
        return 1;
    }
    HalSerialBootMarkForce("Boot: Duo WIFI claim scan ok\n");
    return 1;
}

static int DuoWifiReady(void) {
    return (SdhciWifiReady() && sDuoWifiFirmwareLoaded) ? 1 : 0;
}

static int DuoWifiScan(void) {
    if (!DuoWifiReady()) {
        return -1;
    }
    return SdhciWifiScan(AIC_SCAN_MODE_RUN, 0);
}

static UINT32 DuoWifiScanCount(void) {
    return AicScanReportCount();
}

static int DuoWifiScanEntryAt(UINT32 Index, char *SsidOut, UINT32 SsidCapacity, INT32 *RssiOut,
                              UINT16 *ChannelOut) {
    AIC_SCAN_AP Entry;
    UINT32 Copy;

    if (SsidOut == 0 || SsidCapacity == 0) {
        return -1;
    }
    if (AicScanReportCopyAt(Index, &Entry) != 0) {
        return -1;
    }
    for (Copy = 0; Entry.Ssid[Copy] != 0 && (Copy + 1u) < SsidCapacity; Copy++) {
        SsidOut[Copy] = Entry.Ssid[Copy];
    }
    SsidOut[Copy] = 0;
    if (RssiOut != 0) {
        *RssiOut = (INT32)Entry.Rssi;
    }
    if (ChannelOut != 0) {
        *ChannelOut = (UINT16)Entry.Channel;
    }
    return 0;
}

static int DuoWifiAssociateOpen(const char *Ssid) {
    /* Duo 课网须密码；开放关联不在本 Ops。join 仍走 wififw join。 */
    (void)Ssid;
    return -1;
}

void BoardWifiRegister(void) {
    static const HAL_WIFI_OPS Operations = {
        .Claim = DuoWifiClaim,
        .Ready = DuoWifiReady,
        .Scan = DuoWifiScan,
        .ScanCount = DuoWifiScanCount,
        .ScanEntryAt = DuoWifiScanEntryAt,
        .AssociateOpen = DuoWifiAssociateOpen,
    };

    HalWifiSetOps(&Operations);
}
