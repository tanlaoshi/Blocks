/*
 * ClaimOps.c — Duo HalWifi Ops（对表 Pi BoardWifiRegister）
 *
 * Worker→HalWifiClaim：探 SDIO → 灌片 write+confirm → bringup
 * → 有线 link up：跳过 scan/join（热更走有线；勿再扫拖死壳）
 * → 无有线：scan → 课网 join+DHCP（ClaimNetwork.c）
 *
 * BT：本 Claim 灌 fmacfw；其后 HalBluetoothInitialize 可再切 fmacfwbt（Docs/28）。
 */
#include "Sdhci.h"
#include "Wifi/ScanPrivate.h"
#include "Ethernet.h"
#include "Hal.h"
#include "HalDevices.h"

extern int DuoWifiConnectClassroomIfNoWired(void);

static int sDuoWifiFirmwareLoaded;
static int sDuoWifiClaimScanDone;

static void DuoWifiClaimMark(const char *Text) {
    HalSerialBootMarkForce(Text);
    /* Quiet 白名单也放行；与 Force 双通道，防单路静默 */
    SdhciMark(Text);
}

static int DuoWifiClaim(void) {
    if (sDuoWifiFirmwareLoaded) {
        return 1;
    }

    if (!SdhciWifiReady()) {
        DuoWifiClaimMark("Boot: Duo WIFI claim probe\n");
        if (SdhciWifiProbe(0) != 0) {
            DuoWifiClaimMark("Boot: Duo WIFI claim probe fail\n");
            return 0;
        }
    }

    DuoWifiClaimMark("Boot: Duo WIFI claim fw write\n");
    if (SdhciWifiFw(SDHCI_WIFI_FW_WRITE | SDHCI_WIFI_FW_CONFIRM) != 0) {
        DuoWifiClaimMark("Boot: Duo WIFI claim fw fail\n");
        return 1;
    }
    sDuoWifiFirmwareLoaded = 1;
    DuoWifiClaimMark("Boot: Duo WIFI claim fw ok\n");

    if (SdhciWifiBringup(0) != 0) {
        DuoWifiClaimMark("Boot: Duo WIFI claim bringup fail\n");
        return 1;
    }
    DuoWifiClaimMark("Boot: Duo WIFI claim bringup ok\n");

    /*
     * 有线：扫/join 只会拖死 Worker（真机曾卡在 scan summary 后数分钟无壳）。
     * 固件已灌，供后续 BT fmacfwbt；网走 dwmac。
     * 判定：BoardEthernetCableIsUp（bind + BMSR，勿经 HalNet）。
     */
    if (BoardEthernetCableIsUp() != 0) {
        DuoWifiClaimMark("Boot: Duo WIFI net skip (wired link up)\n");
        sDuoWifiClaimScanDone = 0;
        return 1;
    }
    DuoWifiClaimMark("Boot: Duo WIFI claim cable down → scan\n");

    if (SdhciWifiScan(AIC_SCAN_MODE_RUN, 0) != 0) {
        DuoWifiClaimMark("Boot: Duo WIFI claim scan fail\n");
        return 1;
    }
    DuoWifiClaimMark("Boot: Duo WIFI claim scan ok\n");
    sDuoWifiClaimScanDone = 1;
    (void)DuoWifiConnectClassroomIfNoWired();
    return 1;
}

static int DuoWifiReady(void) {
    return (SdhciWifiReady() && sDuoWifiFirmwareLoaded) ? 1 : 0;
}

static int DuoWifiScan(void) {
    if (!DuoWifiReady()) {
        return -1;
    }
    /* Worker 在 Claim 后再 Scan：有线跳过；无有线若 Claim 刚扫过则复用表 */
    if (BoardEthernetCableIsUp() != 0) {
        return 0;
    }
    if (sDuoWifiClaimScanDone != 0) {
        sDuoWifiClaimScanDone = 0;
        return 0;
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
