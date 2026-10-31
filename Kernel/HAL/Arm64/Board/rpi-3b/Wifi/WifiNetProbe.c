/*
 * WifiNetProbe.c — pi-wifi-7：DATA↔lwIP 眼睛（0x15c）
 * 7c2：DHCP Discover 引 DATA→LwIpNetifInput。
 * 7c3：武装 netif linkoutput→SDPCM；Kick 前先 LwIpInitialize。
 */
#include "Wifi/Private.h"
#include "Wifi.h"
#ifdef LWIP_ENABLE
#include "Services/LwIp.h"
#endif

#define WIFI_NET_PROBE_ATTEMPTS         800u
#define WIFI_NET_TRANSMIT_POLL_ATTEMPTS 2000u
#define WIFI_NET_ARP_FALLBACK_TARGET    0xAC140A01u /* 172.20.10.1 眼靶 */
#define WIFI_NET_ARP_FALLBACK_SOURCE    0xAC140A02u /* 172.20.10.2 SPA（未 DHCP） */

static UINT32 gWifiNetDataPacketCount;
static UINT32 gWifiNetEventPacketCount;
static UINT32 gWifiNetOtherPacketCount;
static UINT32 gWifiNetPollEmptyCount;
static UINT32 gWifiNetPollErrorCount;
static UINT32 gWifiNetFirstEtherType;
static UINT32 gWifiNetMacOutBytes;
static UINT32 gWifiNetArpTargetAddress;
static UINT32 gWifiNetArpReplyCount;
static UINT32 gWifiNetTransmitOk;
static UINT32 gWifiNetArpOffloadCleared;
static UINT8 gWifiNetMac[6];
static int gWifiNetMacOk;
static int gWifiNetHaveArpTarget;

static void WifiNetPersist(const char *Status) {
    WifiNetProbeLogWrite(Status, gWifiNetMac, gWifiNetMacOk, gWifiNetMacOutBytes,
                         gWifiNetDataPacketCount, gWifiNetEventPacketCount,
                         gWifiNetOtherPacketCount, gWifiNetPollEmptyCount,
                         gWifiNetPollErrorCount, gWifiNetFirstEtherType,
                         gWifiNetArpTargetAddress, gWifiNetTransmitOk,
                         gWifiNetArpReplyCount, WifiNetLwIpInputCount(),
                         BoardWifiNetifLinkOutputCount(), gWifiNetArpOffloadCleared);
}

static int WifiNetDisableArpOffload(void) {
    UINT8 Zero[4];
    Zero[0] = 0;
    Zero[1] = 0;
    Zero[2] = 0;
    Zero[3] = 0;
    /* 对表 Linux brcmf_configure_arp_nd_offload(enable=false) */
    if (WifiIoctlSetVariable("arp_ol", Zero, 4u) != 0) {
        return -1;
    }
    if (WifiIoctlSetVariable("arpoe", Zero, 4u) != 0) {
        return -1;
    }
    return 0;
}

static int WifiNetMacIsAllZero(void) {
    UINT32 Index;
    for (Index = 0; Index < 6u; Index++) {
        if (gWifiNetMac[Index] != 0u) {
            return 0;
        }
    }
    return 1;
}

static int WifiNetReadMac(void) {
    UINT8 Buffer[20];
    UINT32 OutBytes = 0;
    UINT32 Index;

    for (Index = 0; Index < sizeof(Buffer); Index++) {
        Buffer[Index] = 0;
    }
    Buffer[0] = 'c';
    Buffer[1] = 'u';
    Buffer[2] = 'r';
    Buffer[3] = '_';
    Buffer[4] = 'e';
    Buffer[5] = 't';
    Buffer[6] = 'h';
    Buffer[7] = 'e';
    Buffer[8] = 'r';
    Buffer[9] = 'a';
    Buffer[10] = 'd';
    Buffer[11] = 'd';
    Buffer[12] = 'r';
    Buffer[13] = 0;
    if (WifiIoctl(WIFI_SDPCM_GET, WIFI_WLC_GET_VAR, Buffer, 14u + 6u, &OutBytes) != 0) {
        gWifiNetMacOutBytes = OutBytes;
        return -1;
    }
    gWifiNetMacOutBytes = OutBytes;
    if (OutBytes < 6u) {
        return -1;
    }
    for (Index = 0; Index < 6u; Index++) {
        gWifiNetMac[Index] = Buffer[Index];
    }
    if (WifiNetMacIsAllZero()) {
        return -1;
    }
    gWifiNetMacOk = 1;
    return 0;
}

static void WifiNetNoteEthernetPayload(const UINT8 *Payload, UINT32 PayloadBytes, int CountReply) {
    UINT16 EtherType;
    UINT16 Operation;

    if (PayloadBytes < 14u) {
        return;
    }
    EtherType = (UINT16)(((UINT16)Payload[12] << 8) | Payload[13]);
    gWifiNetDataPacketCount++;
    if (gWifiNetFirstEtherType == 0u) {
        gWifiNetFirstEtherType = EtherType;
    }
    /* 7c2：DATA → lwIP（短喂；不改 SMSC 主网卡 / linkoutput） */
    WifiNetFeedEthernetToLwIp(Payload, PayloadBytes);
    if (EtherType != 0x0806u || PayloadBytes < 42u) {
        return;
    }
    Operation = (UINT16)(((UINT16)Payload[20] << 8) | Payload[21]);
    if (!gWifiNetHaveArpTarget && (Operation == 1u || Operation == 2u)) {
        gWifiNetArpTargetAddress =
            ((UINT32)Payload[28] << 24) | ((UINT32)Payload[29] << 16) |
            ((UINT32)Payload[30] << 8) | (UINT32)Payload[31];
        if (gWifiNetArpTargetAddress != 0u) {
            gWifiNetHaveArpTarget = 1;
        }
    }
    if (CountReply && Operation == 2u) {
        gWifiNetArpReplyCount++;
    }
}

static void WifiNetProbeDataWindow(UINT32 Attempts, int CountArpReply) {
    UINT32 Attempt;

    for (Attempt = 0; Attempt < Attempts; Attempt++) {
        UINT8 Channel = 0;
        UINT32 Offset = 0;
        UINT32 Length = 0;
        int Poll;

        if ((Attempt & 0xFFu) == 0u) {
            (void)WifiSdioSleepControlEnsureAwake();
        }
        Poll = WifiPollOnePacket(&Channel, &Offset, &Length);
        if (Poll != 0) {
            if (Poll < 0) {
                gWifiNetPollErrorCount++;
            } else {
                gWifiNetPollEmptyCount++;
            }
            SdhciDelayMicroseconds(1000);
            continue;
        }
        if (Channel == WIFI_SDPCM_EVENT_CHANNEL) {
            gWifiNetEventPacketCount++;
            continue;
        }
        if (Channel != WIFI_SDPCM_DATA_CHANNEL || Length <= Offset + 4u) {
            gWifiNetOtherPacketCount++;
            continue;
        }
        {
            const UINT8 *Bdc = gWifiSdpcmPacket + Offset;
            UINT32 BdcSkip = 4u + ((UINT32)Bdc[3] << 2);
            const UINT8 *Payload;
            UINT32 PayloadBytes;

            if (Offset + BdcSkip + 14u > Length) {
                gWifiNetOtherPacketCount++;
                continue;
            }
            Payload = gWifiSdpcmPacket + Offset + BdcSkip;
            PayloadBytes = Length - (Offset + BdcSkip);
            WifiNetNoteEthernetPayload(Payload, PayloadBytes, CountArpReply);
        }
    }
}

int WifiNetProbeAfterJoin(void) {
    gWifiNetDataPacketCount = 0;
    gWifiNetEventPacketCount = 0;
    gWifiNetOtherPacketCount = 0;
    gWifiNetPollEmptyCount = 0;
    gWifiNetPollErrorCount = 0;
    gWifiNetFirstEtherType = 0;
    gWifiNetMacOutBytes = 0;
    gWifiNetArpTargetAddress = 0;
    gWifiNetArpReplyCount = 0;
    gWifiNetTransmitOk = 0;
    gWifiNetArpOffloadCleared = 0;
    gWifiNetHaveArpTarget = 0;
    gWifiNetMacOk = 0;
    gWifiNetMac[0] = 0;
    WifiNetLwIpFeedReset();

    HalSerialBootMarkForce("Boot: Pi WIFI net probe begin\n");
    if (WifiNetReadMac() != 0) {
        WifiNetPersist("net-mac-fail");
        HalSerialBootMarkForce("Boot: Pi WIFI net mac fail\n");
        return -1;
    }
    if (WifiNetDisableArpOffload() == 0) {
        gWifiNetArpOffloadCleared = 1;
    } else {
        HalSerialBootMarkForce("Boot: Pi WIFI arp offload clear fail\n");
    }
    WifiNetProbeDataWindow(WIFI_NET_PROBE_ATTEMPTS, 0);
    if (!gWifiNetHaveArpTarget) {
        gWifiNetArpTargetAddress = WIFI_NET_ARP_FALLBACK_TARGET;
        gWifiNetHaveArpTarget = 1;
    }
    /* 0x15a：DHCP Discover（ARP SPA 刀已否） */
    if (WifiSdpcmSendDhcpDiscover(gWifiNetMac) != 0) {
        WifiNetPersist("net-tx-fail");
        HalSerialBootMarkForce("Boot: Pi WIFI net dhcp discover fail\n");
        return -1;
    }
    gWifiNetTransmitOk = 1;
    WifiNetProbeDataWindow(WIFI_NET_TRANSMIT_POLL_ATTEMPTS, 1);
    if (WifiNetLwIpInputCount() == 0u) {
        WifiNetPersist("net-lwip-no-rx");
        HalSerialBootMarkForce("Boot: Pi WIFI net lwip no rx\n");
        return -1;
    }
    HalSerialBootMarkForce("Boot: Pi WIFI net lwip in ok\n");
    /* 7c3：netif → SDPCM；未武装时 Try 返回 -1 仍走 SMSC */
    BoardWifiArmNetifLinkOutput(gWifiNetMac);
    {
        int Kick = BoardWifiKickDhcpDiscoverViaNetif();
        if (Kick != 0) {
            const char *Status = "net-lwip-out-arm-fail";
            if (Kick == -2) {
                Status = "net-lwip-out-init-fail";
            } else if (Kick == -3) {
                Status = "net-lwip-out-dhcp-fail";
            }
            WifiNetPersist(Status);
            HalSerialBootMarkForce("Boot: Pi WIFI net lwip out kick fail\n");
            return -1;
        }
    }
#ifdef LWIP_ENABLE
    /* Discover 广播应同步打到 linkoutput；再拍超时供重传眼 */
    LwIpPoll();
#endif
    WifiNetProbeDataWindow(WIFI_NET_PROBE_ATTEMPTS, 1);
#ifdef LWIP_ENABLE
    LwIpPoll();
#endif
    if (BoardWifiNetifLinkOutputCount() > 0u) {
        WifiNetPersist("net-lwip-out-ok");
        HalSerialBootMarkForce("Boot: Pi WIFI net lwip out ok\n");
        return 0;
    }
    WifiNetPersist("net-lwip-out-fail");
    HalSerialBootMarkForce("Boot: Pi WIFI net lwip out fail\n");
    return -1;
}
