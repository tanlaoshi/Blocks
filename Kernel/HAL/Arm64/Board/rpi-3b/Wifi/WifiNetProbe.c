/*
 * WifiNetProbe.c — pi-wifi-7：关联后数据通道眼睛（MAC + DATA 帧计数）
 * 一变量：先证明 F2 DATA 有以太网帧，再接 lwIP/DHCP。
 * 0x152：cur_etheraddr 对表 cyw43 取响应**首** 6B（0x151 取末 6B → 全 0）。
 */
#include "Wifi/Private.h"
#include "FileSystem.h"
#include "Fat.h"

#define WIFI_NET_PROBE_ATTEMPTS 3000u

static UINT32 gWifiNetDataPacketCount;
static UINT32 gWifiNetEventPacketCount;
static UINT32 gWifiNetOtherPacketCount;
static UINT32 gWifiNetPollEmptyCount;
static UINT32 gWifiNetPollErrorCount;
static UINT32 gWifiNetFirstEtherType;
static UINT32 gWifiNetMacOutBytes;
static UINT8 gWifiNetMac[6];
static int gWifiNetMacOk;

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
    char Buffer[512];
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
    /* "cur_etheraddr\0" + 6B 占位；对表 cyw43_ll_wifi_get_mac → 响应**首** 6B */
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

static void WifiNetProbeDataWindow(void) {
    UINT32 Attempt;

    for (Attempt = 0; Attempt < WIFI_NET_PROBE_ATTEMPTS; Attempt++) {
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
            UINT16 EtherType;

            if (Offset + BdcSkip + 14u > Length) {
                gWifiNetOtherPacketCount++;
                continue;
            }
            Payload = gWifiSdpcmPacket + Offset + BdcSkip;
            EtherType = (UINT16)(((UINT16)Payload[12] << 8) | Payload[13]);
            gWifiNetDataPacketCount++;
            if (gWifiNetFirstEtherType == 0u) {
                gWifiNetFirstEtherType = EtherType;
            }
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
    gWifiNetMacOk = 0;
    gWifiNetMac[0] = 0;

    HalSerialBootMarkForce("Boot: Pi WIFI net probe begin\n");
    if (WifiNetReadMac() != 0) {
        WifiNetPersistLog("net-mac-fail");
        HalSerialBootMarkForce("Boot: Pi WIFI net mac fail\n");
        return -1;
    }
    WifiNetProbeDataWindow();
    if (gWifiNetDataPacketCount == 0u) {
        WifiNetPersistLog("net-no-data");
        HalSerialBootMarkForce("Boot: Pi WIFI net no data pkts\n");
        return -1;
    }
    WifiNetPersistLog("net-data-ok");
    HalSerialBootMarkForce("Boot: Pi WIFI net data ok\n");
    return 0;
}
