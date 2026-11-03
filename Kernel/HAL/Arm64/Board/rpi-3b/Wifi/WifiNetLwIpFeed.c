/*
 * WifiNetLwIpFeed.c — WiFi DATA 以太网 → LwIpNetifInput
 * 对表 SMSC Frame.c。武装后 HalNetFeedAlternateReceive 每拍短喂。
 *
 * TX credit 等待也会 Poll：禁在 tcp_output/linkoutput 栈上直接 netif_input
 * （lwIP 非重入；Pi 2026-11-03 header err=1）。Defer 入队，出站后再灌。
 */
#include "Wifi/Private.h"
#include "Wifi.h"
#ifdef LWIP_ENABLE
#include "Netif.h"
#endif

#define WIFI_NET_DEFER_SLOTS 8u
#define WIFI_NET_DEFER_BYTES 1518u

static UINT32 gWifiNetLwIpInputCount;
static int gWifiNetDeferDepth;
static UINT8 gWifiNetDeferPayload[WIFI_NET_DEFER_SLOTS][WIFI_NET_DEFER_BYTES];
static UINT16 gWifiNetDeferLength[WIFI_NET_DEFER_SLOTS];
static UINT32 gWifiNetDeferCount;
static UINT32 gWifiNetDeferRead;
static UINT32 gWifiNetDeferWrite;

void WifiNetLwIpFeedReset(void) {
    gWifiNetLwIpInputCount = 0;
    gWifiNetDeferDepth = 0;
    gWifiNetDeferCount = 0;
    gWifiNetDeferRead = 0;
    gWifiNetDeferWrite = 0;
}

UINT32 WifiNetLwIpInputCount(void) {
    return gWifiNetLwIpInputCount;
}

void WifiNetReceiveDeferPush(void) {
    gWifiNetDeferDepth++;
}

static void WifiNetFeedEthernetDirect(const UINT8 *Frame, UINT32 FrameBytes) {
#ifdef LWIP_ENABLE
    LwIpNetifInput(Frame, FrameBytes);
    gWifiNetLwIpInputCount++;
#else
    (void)Frame;
    (void)FrameBytes;
#endif
}

static void WifiNetFlushDeferredToLwIp(void) {
    while (gWifiNetDeferCount > 0u) {
        UINT32 Slot = gWifiNetDeferRead;
        UINT16 Length = gWifiNetDeferLength[Slot];

        gWifiNetDeferRead = (gWifiNetDeferRead + 1u) % WIFI_NET_DEFER_SLOTS;
        gWifiNetDeferCount--;
        if (Length >= 14u) {
            WifiNetFeedEthernetDirect(gWifiNetDeferPayload[Slot], Length);
        }
    }
}

void WifiNetReceiveDeferPop(void) {
    /*
     * 只减深度，不在此 flush：SendEthernet 仍在 tcp_output/linkoutput 栈上，
     * 此处 netif_input = 重入（0x161 仍 err=1）。由 PollFeed 在 depth==0 时灌。
     */
    if (gWifiNetDeferDepth > 0) {
        gWifiNetDeferDepth--;
    }
}

void WifiNetFeedEthernetToLwIp(const UINT8 *Frame, UINT32 FrameBytes) {
    if (Frame == 0 || FrameBytes < 14u || FrameBytes > WIFI_NET_DEFER_BYTES) {
        return;
    }
    if (gWifiNetDeferDepth > 0) {
        if (gWifiNetDeferCount >= WIFI_NET_DEFER_SLOTS) {
            return;
        }
        {
            UINT32 Index;
            UINT32 Slot = gWifiNetDeferWrite;

            for (Index = 0; Index < FrameBytes; Index++) {
                gWifiNetDeferPayload[Slot][Index] = Frame[Index];
            }
            gWifiNetDeferLength[Slot] = (UINT16)FrameBytes;
            gWifiNetDeferWrite = (gWifiNetDeferWrite + 1u) % WIFI_NET_DEFER_SLOTS;
            gWifiNetDeferCount++;
        }
        return;
    }
    WifiNetFeedEthernetDirect(Frame, FrameBytes);
}

int WifiNetTryFeedPolledPacket(UINT8 Channel, UINT32 Offset, UINT32 Length) {
    const UINT8 *Bdc;
    UINT32 BdcSkip;
    UINT32 PayloadBytes;

    if (Channel != WIFI_SDPCM_DATA_CHANNEL || Length <= Offset + 4u) {
        return 0;
    }
    Bdc = gWifiSdpcmPacket + Offset;
    BdcSkip = 4u + ((UINT32)Bdc[3] << 2);
    if (Offset + BdcSkip + 14u > Length) {
        return 0;
    }
    PayloadBytes = Length - (Offset + BdcSkip);
    WifiNetFeedEthernetToLwIp(gWifiSdpcmPacket + Offset + BdcSkip, PayloadBytes);
    return 1;
}

void BoardWifiPollFeedLwIp(UINT32 MaxPackets) {
    UINT32 Fed = 0u;
    UINT32 Attempts = 0u;

    if (!BoardWifiLinkOutputIsArmed() || MaxPackets == 0u) {
        return;
    }
    /* TX 延后帧：仅 depth==0（已离开 tcp_output）时灌入 lwIP */
    if (gWifiNetDeferDepth == 0) {
        WifiNetFlushDeferredToLwIp();
    }
    (void)WifiSdioSleepControlEnsureAwake();
    (void)WifiBackplaneWrite32(WIFI_SDIO_TO_SB_MAILBOX, 1u << 3);
    /*
     * MaxPackets = 成功喂入（含 defer 入队）的 DATA 帧数。
     * 事件/空包不占预算。
     */
    while (Fed < MaxPackets && Attempts < (MaxPackets * 8u)) {
        UINT8 Channel = 0;
        UINT32 Offset = 0;
        UINT32 Length = 0;
        int Poll;

        Attempts++;
        if ((Attempts & 0x0Fu) == 0u) {
            (void)WifiSdioSleepControlEnsureAwake();
        }
        Poll = WifiPollOnePacket(&Channel, &Offset, &Length);
        if (Poll != 0) {
            return;
        }
        if (WifiNetTryFeedPolledPacket(Channel, Offset, Length) != 0) {
            Fed++;
        }
    }
}
