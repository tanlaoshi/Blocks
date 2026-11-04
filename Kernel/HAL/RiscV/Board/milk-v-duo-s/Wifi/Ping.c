/*
 * WifiPing.c — `wififw ping [ms]`：对 DHCP 网关 ICMP Echo（dwifi-ping / Docs/23）
 *
 * 前置：本开机已 `wififw dhcp` 成功（租约 + router）。流程：ARP 网关 → Echo → `ping_ok`。
 * 对表 Pi wifi-7e 壳级判据；不新开 lwIP 栈。
 */
#include "Wifi/NetPrivate.h"
#include "Wifi/AssocPrivate.h"
#include "Wifi/EapolPrivate.h"
#include "CString.h"

#define AIC_NET_PING_DEFAULT_MS 4000u
#define AIC_NET_PING_MAX_MS 30000u

static int PingParseMilliseconds(const char *Text, UINT32 *OutMilliseconds) {
    UINT32 Value = 0;
    UINT32 Index = 0;

    if (Text == 0 || Text[0] == 0) {
        return -1;
    }
    while (Text[Index] != 0) {
        char One = Text[Index];

        if (One < '0' || One > '9') {
            return -1;
        }
        Value = Value * 10u + (UINT32)(One - '0');
        if (Value > AIC_NET_PING_MAX_MS) {
            return -1;
        }
        Index++;
    }
    if (Value == 0u) {
        return -1;
    }
    *OutMilliseconds = Value;
    return 0;
}

int SdhciWifiPing(const char *MillisecondsText) {
    SDHCI_HOST *Host;
    AIC_NET_DHCP_REPLY Lease;
    UINT8 Station[AIC_NET_MAC_LENGTH];
    UINT8 GatewayMac[AIC_NET_MAC_LENGTH];
    UINT8 VifIndex = 0;
    UINT8 ApIndex = 0;
    UINT32 Budget = AIC_NET_PING_DEFAULT_MS;
    char TargetText[AIC_NET_IPV4_TEXT_MAX];
    int Status;

    if (MillisecondsText != 0 && PingParseMilliseconds(MillisecondsText, &Budget) != 0) {
        SdhciMark("WiFi: ping: bad ms (decimal 1..30000)\n");
        return -1;
    }
    Host = SdhciHostAt(1);
    if (Host == 0 || !SdhciWifiReady()) {
        SdhciMark("WiFi: ping needs a probed card (wifiprobe + wififw join first)\n");
        return -1;
    }
    if (gAicEapolKeysInstalled == 0u) {
        SdhciMark("WiFi: ping: keys not installed (join first)\n");
        return -1;
    }
    if (AicDhcpLeaseGet(&Lease) != 0 || Lease.HasRouter == 0u) {
        SdhciMark("WiFi: ping: no lease/router (run wififw dhcp first)\n");
        return -1;
    }
    if (AicNetStationAddress(Station) != 0) {
        SdhciMark("WiFi: ping: no station MAC\n");
        return -1;
    }
    if (AicWifiVendorPrepare(Host) != 0) {
        SdhciMark("WiFi: ping: vendor prepare failed\n");
        return -1;
    }
    AicJoinLastIndexes(&VifIndex, &ApIndex);
    AicNetFormatIpv4(Lease.Router, TargetText);
    SdhciMark("WiFi: ping: target=");
    SdhciMark(TargetText);
    SdhciMark(" from=");
    AicNetFormatIpv4(Lease.YourIp, TargetText);
    SdhciMark(TargetText);
    SdhciMark(" budget=");
    SdhciMarkDecimal("", Budget);
    SdhciMark("ms\n");

    AicNetStart();
    if (AicArpResolveGateway(Host, Station, Lease.YourIp, Lease.Router, VifIndex, ApIndex,
                             GatewayMac, AIC_NET_ARP_BUDGET_MS) != 0) {
        AicNetStop();
        SdhciMark("WiFi: ping_ok=0 (ARP gateway failed)\n");
        return -1;
    }
    Status = AicIcmpEchoUnicast(Host, Station, Lease.YourIp, Lease.Router, GatewayMac, VifIndex,
                                ApIndex, Budget);
    AicNetStop();
    if (Status != 0) {
        SdhciMark("WiFi: ping_ok=0 (no echo reply)\n");
        return -1;
    }
    SdhciMark("WiFi: ping_ok\n");
    SdhciMark("WiFi: ping_ok=1\n");
    return 0;
}
