/*
 * WifiNetProbe.c — pi-wifi-7：关联后 SDPCM TX ARP 眼睛（0x154）
 * 0x153 否证：无被动 DATA 就永不 TX。本刀 MAC 后必发 who-has；
 * 有 RX SPA 用 SPA，否则眼靶 172.20.10.1（课网 Apple 热点常见网关，仅眼睛）。
 * 本刀不接 lwIP。
 */
#include "Wifi/Private.h"
#include "FileSystem.h"
#include "Fat.h"

#define WIFI_NET_PROBE_ATTEMPTS         800u  /* 可选学 SPA；勿再 3s 卡死 */
#define WIFI_NET_TRANSMIT_POLL_ATTEMPTS 1500u
/* 课网 SSID Apple（个人热点）常见网关；无被动 ARP 时的 TX 眼靶 */
#define WIFI_NET_ARP_FALLBACK_TARGET    0xAC140A01u /* 172.20.10.1 */

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
static UINT8 gWifiNetMac[6];
static int gWifiNetMacOk;
static int gWifiNetHaveArpTarget;

static void WifiNetAppendText(char *Buffer, UINTN Capacity, UINTN *Length, const char *Text) {
    UINTN Index;
    if (Text == 0) {
        return;
    }
    for (Index = 0; Text[Index] != 0 && *Length + 1u < Capacity; Index++) {
        Buffer[(*Length)++] = Text[Index];
    }
}

static void WifiNetAppendDecimal(char *Buffer, UINTN Capacity, UINTN *Length, UINT32 Value) {
    char Digits[12];
    UINT32 DigitCount = 0;
    if (Value == 0u) {
        Digits[DigitCount++] = '0';
    } else {
        while (Value != 0u && DigitCount < sizeof(Digits)) {
            Digits[DigitCount++] = (char)('0' + (Value % 10u));
            Value /= 10u;
        }
    }
    while (DigitCount > 0u && *Length + 1u < Capacity) {
        Buffer[(*Length)++] = Digits[--DigitCount];
    }
}

static void WifiNetAppendHexByte(char *Buffer, UINTN Capacity, UINTN *Length, UINT8 Value) {
    static const char HexDigits[] = "0123456789abcdef";
    if (*Length + 2u >= Capacity) {
        return;
    }
    Buffer[(*Length)++] = HexDigits[(Value >> 4) & 0xFu];
    Buffer[(*Length)++] = HexDigits[Value & 0xFu];
}

static void WifiNetAppendFieldDecimal(char *Buffer, UINTN Capacity, UINTN *Length,
                                      const char *Name, UINT32 Value) {
    WifiNetAppendText(Buffer, Capacity, Length, Name);
    WifiNetAppendDecimal(Buffer, Capacity, Length, Value);
    WifiNetAppendText(Buffer, Capacity, Length, "\n");
}

static void WifiNetPersistLog(const char *Status) {
    char Buffer[640];
    UINTN Length = 0;
    static const char HexDigits[] = "0123456789abcdef";
    UINT32 Tag = WIFI_BUILD_TAG;
    INT32 Nibble;
    UINT32 Index;

    WifiNetAppendText(Buffer, sizeof(Buffer), &Length, "tag=");
    for (Nibble = 7; Nibble >= 0; Nibble--) {
        if (Length + 1u >= sizeof(Buffer)) {
            break;
        }
        Buffer[Length++] = HexDigits[(Tag >> (UINT32)(Nibble * 4)) & 0xFu];
    }
    WifiNetAppendText(Buffer, sizeof(Buffer), &Length, "\nstatus=");
    WifiNetAppendText(Buffer, sizeof(Buffer), &Length, Status ? Status : "?");
    WifiNetAppendText(Buffer, sizeof(Buffer), &Length, "\nmac=");
    if (gWifiNetMacOk) {
        for (Index = 0; Index < 6u; Index++) {
            if (Index != 0u) {
                WifiNetAppendText(Buffer, sizeof(Buffer), &Length, ":");
            }
            WifiNetAppendHexByte(Buffer, sizeof(Buffer), &Length, gWifiNetMac[Index]);
        }
    } else {
        WifiNetAppendText(Buffer, sizeof(Buffer), &Length, "?");
    }
    WifiNetAppendText(Buffer, sizeof(Buffer), &Length, "\n");
    WifiNetAppendFieldDecimal(Buffer, sizeof(Buffer), &Length, "mac_out=", gWifiNetMacOutBytes);
    WifiNetAppendFieldDecimal(Buffer, sizeof(Buffer), &Length, "data_pkts=",
                              gWifiNetDataPacketCount);
    WifiNetAppendFieldDecimal(Buffer, sizeof(Buffer), &Length, "event_pkts=",
                              gWifiNetEventPacketCount);
    WifiNetAppendFieldDecimal(Buffer, sizeof(Buffer), &Length, "other_pkts=",
                              gWifiNetOtherPacketCount);
    WifiNetAppendFieldDecimal(Buffer, sizeof(Buffer), &Length, "poll_empty=",
                              gWifiNetPollEmptyCount);
    WifiNetAppendFieldDecimal(Buffer, sizeof(Buffer), &Length, "poll_error=",
                              gWifiNetPollErrorCount);
    WifiNetAppendFieldDecimal(Buffer, sizeof(Buffer), &Length, "first_etype=",
                              gWifiNetFirstEtherType);
    WifiNetAppendFieldDecimal(Buffer, sizeof(Buffer), &Length, "arp_target=",
                              gWifiNetArpTargetAddress);
    WifiNetAppendFieldDecimal(Buffer, sizeof(Buffer), &Length, "transmit_ok=",
                              gWifiNetTransmitOk);
    WifiNetAppendFieldDecimal(Buffer, sizeof(Buffer), &Length, "arp_reply=",
                              gWifiNetArpReplyCount);
    Buffer[Length] = 0;
    if (FileSystemWriteFile("WIFI-NET.LOG", Buffer, Length) != FAT_OK) {
        HalSerialBootMarkForce("Boot: Pi WIFI net log write fail\n");
        return;
    }
    (void)FileSystemFileSync("WIFI-NET.LOG");
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
    if (EtherType != 0x0806u || PayloadBytes < 42u) {
        return;
    }
    Operation = (UINT16)(((UINT16)Payload[20] << 8) | Payload[21]);
    if (!gWifiNetHaveArpTarget && (Operation == 1u || Operation == 2u)) {
        /* SPA @ eth+28；记 ARP 对端 IP，作 who-has 目标 */
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
    gWifiNetHaveArpTarget = 0;
    gWifiNetMacOk = 0;
    gWifiNetMac[0] = 0;

    HalSerialBootMarkForce("Boot: Pi WIFI net probe begin\n");
    if (WifiNetReadMac() != 0) {
        WifiNetPersistLog("net-mac-fail");
        HalSerialBootMarkForce("Boot: Pi WIFI net mac fail\n");
        return -1;
    }
    /* 短窗学 SPA（有最好）；无也不拦 TX——0x153 否证 */
    WifiNetProbeDataWindow(WIFI_NET_PROBE_ATTEMPTS, 0);
    if (!gWifiNetHaveArpTarget) {
        gWifiNetArpTargetAddress = WIFI_NET_ARP_FALLBACK_TARGET;
        gWifiNetHaveArpTarget = 1;
    }
    if (WifiSdpcmSendArpWhoHas(gWifiNetMac, gWifiNetArpTargetAddress) != 0) {
        WifiNetPersistLog("net-tx-fail");
        HalSerialBootMarkForce("Boot: Pi WIFI net tx fail\n");
        return -1;
    }
    gWifiNetTransmitOk = 1;
    WifiNetProbeDataWindow(WIFI_NET_TRANSMIT_POLL_ATTEMPTS, 1);
    if (gWifiNetArpReplyCount == 0u) {
        WifiNetPersistLog("net-tx-ok");
        HalSerialBootMarkForce("Boot: Pi WIFI net tx ok (no arp reply)\n");
        return 0;
    }
    WifiNetPersistLog("net-arp-reply-ok");
    HalSerialBootMarkForce("Boot: Pi WIFI net arp reply ok\n");
    return 0;
}
