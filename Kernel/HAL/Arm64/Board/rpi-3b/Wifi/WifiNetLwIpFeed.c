/*
 * WifiNetLwIpFeed.c — pi-wifi-7c2/7d：WiFi DATA 以太网 → LwIpNetifInput
 * 对表 SMSC Frame.c。武装后 HalNetFeedAlternateReceive 每拍短喂。
 * 短路径、无 IrqSave 长持；勿在此 Present/整屏刷新。
 */
#include "Wifi/Private.h"
#include "Wifi.h"
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

void BoardWifiPollFeedLwIp(UINT32 MaxPackets) {
    UINT32 Fed;

    if (!BoardWifiLinkOutputIsArmed() || MaxPackets == 0u) {
        return;
    }
    for (Fed = 0; Fed < MaxPackets; Fed++) {
        UINT8 Channel = 0;
        UINT32 Offset = 0;
        UINT32 Length = 0;
        int Poll;
        const UINT8 *Bdc;
        UINT32 BdcSkip;
        UINT32 PayloadBytes;

        Poll = WifiPollOnePacket(&Channel, &Offset, &Length);
        if (Poll != 0) {
            return;
        }
        if (Channel != WIFI_SDPCM_DATA_CHANNEL || Length <= Offset + 4u) {
            continue;
        }
        Bdc = gWifiSdpcmPacket + Offset;
        BdcSkip = 4u + ((UINT32)Bdc[3] << 2);
        if (Offset + BdcSkip + 14u > Length) {
            continue;
        }
        PayloadBytes = Length - (Offset + BdcSkip);
        WifiNetFeedEthernetToLwIp(gWifiSdpcmPacket + Offset + BdcSkip, PayloadBytes);
    }
}
