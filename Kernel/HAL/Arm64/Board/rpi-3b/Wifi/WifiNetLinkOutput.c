/*
 * WifiNetLinkOutput.c — pi-wifi-7c3/7d：lwIP linkoutput → SDPCM DATA
 * 武装后 BoardWifiTryLinkOutput 优先于 SMSC；未武装返回 -1（走有线）。
 * 短路径、无长 IrqSave；勿在此 Present。
 *
 * 0x15b 否证：Claim 早于 LwIpInitialize → dhcp_start ERR_ARG。
 * 0x15c：Kick 前 LwIpInitialize。
 * 0x15d：Kick 走 LwIpDhcpRestart（SyncMac 用站点 MAC）；F2 喂在 Feed 拍。
 */
#include "Wifi/Private.h"
#ifdef LWIP_ENABLE
#include "Netif.h"
#include "Services/LwIp.h"
#include "lwip/netif.h"
#endif

static int gWifiNetLinkOutputArmed;
static UINT32 gWifiNetLinkOutputCount;
static UINT8 gWifiNetLinkOutputMac[6];
static int gWifiNetLinkOutputMacOk;

void BoardWifiArmNetifLinkOutput(const UINT8 StationMac[6]) {
    UINT32 Index;
    gWifiNetLinkOutputArmed = 0;
    gWifiNetLinkOutputMacOk = 0;
    gWifiNetLinkOutputCount = 0;
    if (StationMac == 0) {
        return;
    }
    for (Index = 0; Index < 6u; Index++) {
        gWifiNetLinkOutputMac[Index] = StationMac[Index];
    }
    gWifiNetLinkOutputMacOk = 1;
    gWifiNetLinkOutputArmed = 1;
}

int BoardWifiLinkOutputIsArmed(void) {
    return gWifiNetLinkOutputArmed ? 1 : 0;
}

int BoardWifiCopyStationMacIfArmed(UINT8 Mac[6]) {
    UINT32 Index;
    if (!gWifiNetLinkOutputArmed || !gWifiNetLinkOutputMacOk || Mac == 0) {
        return -1;
    }
    for (Index = 0; Index < 6u; Index++) {
        Mac[Index] = gWifiNetLinkOutputMac[Index];
    }
    return 0;
}

UINT32 BoardWifiNetifLinkOutputCount(void) {
    return gWifiNetLinkOutputCount;
}

int BoardWifiTryLinkOutput(const UINT8 *Frame, UINTN FrameLength) {
    if (!gWifiNetLinkOutputArmed || Frame == 0 || FrameLength < 14u) {
        return -1;
    }
    if (FrameLength > 1514u) {
        return -1;
    }
    if (WifiSdpcmSendEthernet(Frame, (UINT32)FrameLength) != 0) {
        return -1;
    }
    gWifiNetLinkOutputCount++;
    return 0;
}

int BoardWifiKickDhcpDiscoverViaNetif(void) {
#ifdef LWIP_ENABLE
    struct netif *Netif;
    UINT32 Index;

    if (!gWifiNetLinkOutputArmed || !gWifiNetLinkOutputMacOk) {
        return -1;
    }
    if (!LwIpActive() && LwIpInitialize() != 0) {
        return -2;
    }
    Netif = LwIpNetifGet();
    if (Netif == 0 || !netif_is_up(Netif)) {
        return -2;
    }
    for (Index = 0; Index < 6u; Index++) {
        Netif->hwaddr[Index] = gWifiNetLinkOutputMac[Index];
    }
    /* 对表 Dhcp.c：Enqueue→SyncMac（HalNetGetMac=站点）+ dhcp_start；Worker/探针可 Step */
    if (LwIpDhcpRestart(12000) != 0) {
        return -3;
    }
    return 0;
#else
    return -1;
#endif
}
