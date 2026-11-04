/*
 * ClaimNetwork.c — 无有线则课网 join + DHCP（对表 Pi WifiJoinAfterScan）
 *
 * 有线：ClaimOps 在 bringup 后已 net skip，本函数再防一层。
 * 无有线：扫表须有 Apple → WPA2 join → SdhciWifiDhcp → AdoptLease。
 */
#include "Sdhci.h"
#include "Wifi/AssocPrivate.h"
#include "Wifi/NetPrivate.h"
#include "Wifi/ScanPrivate.h"
#include "Ethernet.h"
#include "CString.h"
#include "Hal.h"
#include "HalDevices.h"

#define DUO_CLASSROOM_SSID "Apple"
#define DUO_CLASSROOM_PASSWORD "Tank1989"

static int DuoClassroomSeenInScan(void) {
    UINT32 Count = AicScanReportCount();
    UINT32 Index;
    AIC_SCAN_AP Entry;

    for (Index = 0; Index < Count; Index++) {
        if (AicScanReportCopyAt(Index, &Entry) != 0) {
            continue;
        }
        if (Entry.Ssid[0] == 0) {
            continue;
        }
        if (StringEqual(Entry.Ssid, DUO_CLASSROOM_SSID) != 0) {
            return 1;
        }
    }
    return 0;
}

int DuoWifiConnectClassroomIfNoWired(void) {
    if (BoardEthernetCableIsUp() != 0) {
        HalSerialBootMarkForce("Boot: Duo WIFI net skip (wired link up)\n");
        SdhciMark("Boot: Duo WIFI net skip (wired link up)\n");
        return 0;
    }

    if (DuoClassroomSeenInScan() == 0) {
        HalSerialBootMarkForce("Boot: Duo WIFI claim join-no-apple\n");
        return 0;
    }

    HalSerialBootMarkForce("Boot: Duo WIFI claim join begin\n");
    if (SdhciWifiJoin(0, DUO_CLASSROOM_SSID, 0, DUO_CLASSROOM_PASSWORD) != 0) {
        HalSerialBootMarkForce("Boot: Duo WIFI claim join fail\n");
        return -1;
    }
    HalSerialBootMarkForce("Boot: Duo WIFI claim join ok\n");

    HalSerialBootMarkForce("Boot: Duo WIFI claim dhcp begin\n");
    if (SdhciWifiDhcp(0) != 0) {
        HalSerialBootMarkForce("Boot: Duo WIFI claim dhcp fail\n");
        return -1;
    }
    HalSerialBootMarkForce("Boot: Duo WIFI claim dhcp ok\n");
    return 0;
}
