/*
 * WifiNetLwIpFeed.c — pi-wifi-7c2：WiFi DATA 以太网 → LwIpNetifInput
 * 对表 SMSC Frame.c（gSmscLwIpReceive 时入栈）。本刀只喂 RX，不改 linkoutput/主网卡。
 * 注释：短路径、无 IrqSave 长持；勿在此 Present/整屏刷新。
 */
#include "Wifi/Private.h"
#ifdef LWIP_ENABLE
#include "Netif.h"
#endif

static UINT32 gWifiNetLwIpInputCount;

void WifiNetLwIpFeedReset(void) {
    gWifiNetLwIpInputCount = 0;
}

UINT32 WifiNetLwIpInputCount(void) {
    return gWifiNetLwIpInputCount;
}

void WifiNetFeedEthernetToLwIp(const UINT8 *Frame, UINT32 FrameBytes) {
    if (Frame == 0 || FrameBytes < 14u || FrameBytes > 1518u) {
        return;
    }
#ifdef LWIP_ENABLE
    LwIpNetifInput(Frame, FrameBytes);
    gWifiNetLwIpInputCount++;
#else
    (void)Frame;
    (void)FrameBytes;
#endif
}
