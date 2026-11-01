/*
 * WifiDhcpProbe.c — BLOCKS:probe.cfg 探针（从 WifiDhcp.c 拆出，≤300）
 *
 * 键可出现在文件任意位置（空格/换行分隔均可）：
 *   mcast=<n>  覆盖组播 hostdesc.staid（-1 = ap_idx）
 *   ac=<n> / tid=<n>  覆盖组播 ac/tid
 *   arpuni=1   单播 ARP 证伪（Docs/19 → unicast-data-ok|fail）
 *   start=1    EAPOL-Start 探针
 */
#include "WifiNetPrivate.h"
#include "FileSystem.h"
#include "WifiAssocPrivate.h"
#include "WifiEapolPrivate.h"
#include "CString.h"

static int ProbeMatch(const char *Text, UINTN Got, UINTN Offset, const char *Key) {
    UINTN Index = 0;

    while (Key[Index] != 0) {
        if (Offset + Index >= Got || Text[Offset + Index] != Key[Index]) {
            return 0;
        }
        Index++;
    }
    return 1;
}

static int ProbeParseDecimal(const char *Text, UINTN Got, UINTN Offset, int *OutValue) {
    int Value = 0;
    int Sign = 1;
    UINTN Index = Offset;

    if (Index < Got && Text[Index] == '-') {
        Sign = -1;
        Index++;
    }
    if (Index >= Got || Text[Index] < '0' || Text[Index] > '9') {
        return 0;
    }
    while (Index < Got && Text[Index] >= '0' && Text[Index] <= '9') {
        Value = Value * 10 + (Text[Index] - '0');
        Index++;
    }
    *OutValue = Sign * Value;
    return 1;
}

/*
 * 探针：发一条 EAPOL-Start（单播给 AP），盯 3 秒看有没有 Key 帧回来。
 * AP 回 M1 ⇒ 认得我们；不回 ⇒ 问题更可能在 M4。
 */
static void DhcpProbeEapolStart(void) {
    UINT8 Frame[4];
    UINT8 Bssid[AIC_NET_MAC_LENGTH];
    UINT8 Station[AIC_NET_MAC_LENGTH];
    UINT8 VifIndex = 0;
    UINT8 ApIndex = 0;
    static AIC_EAPOL_COUNTERS Counters;

    if (AicJoinBssid(Bssid) != 0 || AicNetStationAddress(Station) != 0) {
        SdhciMark("WiFi: eapol-start: no BSSID/station (join first)\n");
        return;
    }
    AicJoinLastIndexes(&VifIndex, &ApIndex);
    Frame[0] = 2u;
    Frame[1] = (UINT8)AIC_EAPOL_TYPE_START;
    Frame[2] = 0u;
    Frame[3] = 0u;
    if (AicEapolSend(SdhciHostAt(1), Frame, 4u, Bssid, Station, VifIndex, ApIndex) != 0) {
        SdhciMark("WiFi: eapol-start: send failed\n");
        return;
    }
    SdhciMark("WiFi: eapol-start: sent (unicast to the AP)\n");
    memset(&Counters, 0, sizeof(Counters));
    (void)AicEapolPollRun(SdhciHostAt(1), 3000u, &Counters, 0);
    SdhciMark("WiFi: eapol-start: after 3000ms frames=");
    SdhciMarkDecimal("", Counters.Frames);
    SdhciMark(" key=");
    SdhciMarkDecimal("", Counters.KeyFrames);
    SdhciMark(Counters.KeyFrames != 0u ? " (the AP answered: it sees our unicast frames)\n"
                                       : " (no answer: the AP is not responding to us at all)\n");
}

/* 单播 ARP 问 AP（192.168.31.1）；收口标签见 Docs/19 */
static void DhcpProbeArpUnicast(void) {
    UINT8 Station[AIC_NET_MAC_LENGTH];
    UINT8 Bssid[AIC_NET_MAC_LENGTH];
    UINT8 Mac[AIC_NET_MAC_LENGTH];
    UINT8 VifIndex = 0;
    UINT8 ApIndex = 0;
    static const UINT8 StationIp[4] = {192u, 168u, 31u, 250u};
    static const UINT8 TargetIp[4] = {192u, 168u, 31u, 1u};

    if (AicJoinBssid(Bssid) != 0 || AicNetStationAddress(Station) != 0) {
        SdhciMark("WiFi: arp uni: no BSSID (join first)\n");
        return;
    }
    AicJoinLastIndexes(&VifIndex, &ApIndex);
    (void)AicArpResolveUnicast(SdhciHostAt(1), Station, StationIp, TargetIp, Bssid, VifIndex,
                               ApIndex, Mac, 2000u);
}

void AicWifiDhcpLoadProbe(void) {
    char Text[128];
    UINTN Got = 0;
    UINTN Offset;
    int Value = 0;
    int RunArpUnicast = 0;
    int RunEapolStart = 0;

    if (FileSystemReadFileAt("BLOCKS:probe.cfg", 0, Text, sizeof(Text) - 1u, &Got) != 0 ||
        Got == 0u) {
        return;
    }
    Text[Got] = 0;
    for (Offset = 0; Offset < Got; Offset++) {
        if (ProbeMatch(Text, Got, Offset, "ac=") &&
            ProbeParseDecimal(Text, Got, Offset + 3u, &Value)) {
            AicWifiSetMcastAc((UINT32)Value);
        }
        if (ProbeMatch(Text, Got, Offset, "tid=") &&
            ProbeParseDecimal(Text, Got, Offset + 4u, &Value)) {
            AicWifiSetMcastTid((UINT32)Value);
        }
        if (ProbeMatch(Text, Got, Offset, "mcast=") &&
            ProbeParseDecimal(Text, Got, Offset + 6u, &Value)) {
            AicWifiSetMcastSta((UINT32)Value);
        }
        if (ProbeMatch(Text, Got, Offset, "arpuni=1")) {
            RunArpUnicast = 1;
        }
        if (ProbeMatch(Text, Got, Offset, "start=1")) {
            RunEapolStart = 1;
        }
    }
    if (RunArpUnicast != 0) {
        DhcpProbeArpUnicast();
    }
    if (RunEapolStart != 0) {
        DhcpProbeEapolStart();
    }
}
