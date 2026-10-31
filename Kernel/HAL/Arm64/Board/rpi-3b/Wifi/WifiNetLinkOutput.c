/*
 * WifiNetLinkOutput.c — pi-wifi-7c3：lwIP linkoutput → SDPCM DATA
 * 武装后 BoardWifiTryLinkOutput 优先于 SMSC；未武装返回 -1（走有线）。
 * 短路径、无长 IrqSave；勿在此 Present。
 *
 * 0x15b 否证：Claim/Scan 在 NetworkInitialize，早于 LwIpInitialize；
 * dhcp_start 要求 netif_is_up → ERR_ARG → net-lwip-out-arm-fail。
 * 0x15c：Kick 前对表 Dhcp.c 先 LwIpInitialize。
 */
#include "Wifi/Private.h"
#ifdef LWIP_ENABLE
#include "Netif.h"
#include "Services/LwIp.h"
#include "lwip/dhcp.h"
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
    /* 对表 Services/Net/LwIp/Dhcp.c：Join 探针可能早于 epoch 的 LwIpInitialize */
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
    LwIpDhcpStop();
    if (dhcp_start(Netif) != ERR_OK) {
        return -3;
    }
    return 0;
#else
    return -1;
#endif
}
