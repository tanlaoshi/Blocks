/*
 * WifiScan.c — radio up / escan / 开放关联（pwifi-2）
 */
#include "Wifi/Private.h"

BOARD_WIFI_SCAN_ENTRY gWifiScanEntries[WIFI_SCAN_RESULT_MAXIMUM];
UINT32 gWifiScanEntryCount;
static int gWifiRadioUp;

#define WIFI_EVENT_ESCAN_RESULT 69u
#define WIFI_ESCAN_STATUS_PARTIAL 8u
#define WIFI_ESCAN_STATUS_SUCCESS 0u

static void WifiMemorySet(void *Destination, UINT8 Value, UINT32 Bytes) {
    UINT8 *Cursor = (UINT8 *)Destination;
    UINT32 Index;
    for (Index = 0; Index < Bytes; Index++) {
        Cursor[Index] = Value;
    }
}

static void WifiMemoryCopy(void *Destination, const void *Source, UINT32 Bytes) {
    UINT8 *Out = (UINT8 *)Destination;
    const UINT8 *In = (const UINT8 *)Source;
    UINT32 Index;
    for (Index = 0; Index < Bytes; Index++) {
        Out[Index] = In[Index];
    }
}

static UINT32 WifiGetLittle32(const UINT8 *Source) {
    return (UINT32)Source[0] | ((UINT32)Source[1] << 8) | ((UINT32)Source[2] << 16) |
           ((UINT32)Source[3] << 24);
}

static UINT16 WifiGetLittle16(const UINT8 *Source) {
    return (UINT16)Source[0] | ((UINT16)Source[1] << 8);
}

static INT16 WifiGetLittleSigned16(const UINT8 *Source) {
    return (INT16)WifiGetLittle16(Source);
}

static int WifiPollOnePacket(UINT8 *OutChannel, UINT32 *OutOffset, UINT32 *OutLength) {
    UINT8 HeaderBytes[4];
    UINT16 Size;
    UINT16 SizeComplement;
    UINT32 Aligned;
    UINT8 Channel;
    UINT8 HeaderLength;

    if (WifiSdioCommand53Read(SDIO_FUNCTION_2, 0, HeaderBytes, 4u, 0) != 0) {
        return -1;
    }
    Size = WifiGetLittle16(HeaderBytes);
    SizeComplement = WifiGetLittle16(HeaderBytes + 2);
    if (Size == 0 && SizeComplement == 0) {
        return 1; /* 空 */
    }
    if ((UINT16)(Size ^ SizeComplement) != 0xFFFFu || Size < WIFI_SDPCM_HEADER_BYTES) {
        return -1;
    }
    Aligned = Size - 4u;
    if (Aligned <= 8u) {
        Aligned = (Aligned + 7u) & ~7u;
    } else if (Aligned <= 16u) {
        Aligned = (Aligned + 15u) & ~15u;
    } else {
        Aligned = (Aligned + 63u) & ~63u;
    }
    if (Aligned + 4u > sizeof(gWifiSdpcmPacket)) {
        return -1;
    }
    if (WifiSdioCommand53Read(SDIO_FUNCTION_2, 0, gWifiSdpcmPacket + 4, Aligned, 0) != 0) {
        return -1;
    }
    gWifiSdpcmPacket[0] = HeaderBytes[0];
    gWifiSdpcmPacket[1] = HeaderBytes[1];
    gWifiSdpcmPacket[2] = HeaderBytes[2];
    gWifiSdpcmPacket[3] = HeaderBytes[3];
    Channel = (UINT8)(gWifiSdpcmPacket[5] & 0x0Fu);
    HeaderLength = gWifiSdpcmPacket[7];
    gWifiSdpcmBusCredit = gWifiSdpcmPacket[9];
    *OutChannel = Channel;
    *OutOffset = HeaderLength;
    *OutLength = Size;
    return 0;
}

static int WifiHandleEscanEvent(const UINT8 *Event, UINT32 Length) {
    UINT32 EventType;
    UINT32 Status;
    const UINT8 *Scan;
    UINT8 SsidLength;
    UINT32 Index;

    if (Length < 48u) {
        return 0;
    }
    /* cyw43_async_event：flags@2, type@4, status@8；scan_result 在 union */
    EventType = WifiGetLittle32(Event + 4);
    Status = WifiGetLittle32(Event + 8);
    if (EventType != WIFI_EVENT_ESCAN_RESULT) {
        return 0;
    }
    if (Status == WIFI_ESCAN_STATUS_SUCCESS) {
        return 2; /* 扫完 */
    }
    if (Status != WIFI_ESCAN_STATUS_PARTIAL) {
        return 0;
    }
    /* scan_result @ async_event.u；对表 cyw43_ev_scan_result_t */
    Scan = Event + 48;
    if (Length < 48u + 92u) {
        return 0;
    }
    SsidLength = Scan[30];
    if (SsidLength == 0 || SsidLength > WIFI_SSID_MAXIMUM_BYTES) {
        return 0;
    }
    if (gWifiScanEntryCount >= WIFI_SCAN_RESULT_MAXIMUM) {
        return 0;
    }
    for (Index = 0; Index < gWifiScanEntryCount; Index++) {
        if (gWifiScanEntries[Index].SsidLength == SsidLength) {
            UINT32 Compare;
            int Same = 1;
            for (Compare = 0; Compare < SsidLength; Compare++) {
                if ((UINT8)gWifiScanEntries[Index].Ssid[Compare] != Scan[31 + Compare]) {
                    Same = 0;
                    break;
                }
            }
            if (Same) {
                return 0;
            }
        }
    }
    {
        BOARD_WIFI_SCAN_ENTRY *Entry = &gWifiScanEntries[gWifiScanEntryCount];
        Entry->SsidLength = SsidLength;
        WifiMemoryCopy(Entry->Ssid, Scan + 31, SsidLength);
        Entry->Ssid[SsidLength] = 0;
        Entry->Channel = WifiGetLittle16(Scan + 83);
        Entry->SignalRssi = (INT32)WifiGetLittleSigned16(Scan + 89);
        gWifiScanEntryCount++;
    }
    return 0;
}

int WifiRadioBringUp(void) {
    UINT8 Buffer[48];
    UINT32 Zero = 0;
    UINT32 One = 1;

    if (!gWifiFirmwareLoaded) {
        return -1;
    }
    if (gWifiRadioUp) {
        return 0;
    }
    WifiMemorySet(Buffer, 0, sizeof(Buffer));
    /* bus:txglom=0 */
    WifiMemoryCopy(Buffer, &Zero, 4);
    if (WifiIoctlSetVariable("bus:txglom", Buffer, 4u) != 0) {
        /* 非致命 */
    }
    WifiMemoryCopy(Buffer, &One, 4);
    (void)WifiIoctlSetVariable("apsta", Buffer, 4u);

    /* event_msgs：name\0 + ifindex(u32) + 19B mask；开 escan(69) */
    WifiMemorySet(Buffer, 0, sizeof(Buffer));
    WifiMemoryCopy(Buffer, "bsscfg:event_msgs", 17);
    Buffer[17] = 0;
    Buffer[22 + (WIFI_EVENT_ESCAN_RESULT / 8u)] =
        (UINT8)(1u << (WIFI_EVENT_ESCAN_RESULT % 8u));
    if (WifiIoctl(WIFI_SDPCM_SET, WIFI_WLC_SET_VAR, Buffer, 18u + 4u + 19u, 0) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI event_msgs fail\n");
        return -1;
    }
    SdhciDelayMicroseconds(50000);
    if (WifiIoctl(WIFI_SDPCM_SET, WIFI_WLC_UP, 0, 0, 0) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI WLC_UP fail\n");
        return -1;
    }
    SdhciDelayMicroseconds(50000);
    gWifiRadioUp = 1;
    HalSerialBootMarkForce("Boot: Pi WIFI radio up\n");
    return 0;
}

int WifiScanAccessPoints(void) {
    UINT8 Options[72];
    UINT32 Attempt;
    INT32 MinusOne = -1;

    gWifiScanEntryCount = 0;
    if (WifiFirmwareLoad() != 0) {
        return -1;
    }
    if (WifiRadioBringUp() != 0) {
        return -1;
    }

    WifiMemorySet(Options, 0, sizeof(Options));
    Options[0] = 1; /* version */
    Options[4] = 1; /* action START */
    WifiMemorySet(Options + 44, 0xFF, 6); /* bssid */
    Options[50] = 2;                      /* bss_type ANY */
    WifiMemoryCopy(Options + 52, &MinusOne, 4);
    WifiMemoryCopy(Options + 56, &MinusOne, 4);
    WifiMemoryCopy(Options + 60, &MinusOne, 4);
    WifiMemoryCopy(Options + 64, &MinusOne, 4);

    if (WifiIoctlSetVariable("escan", Options, sizeof(Options)) != 0) {
        return -1;
    }

    for (Attempt = 0; Attempt < 8000u; Attempt++) {
        UINT8 Channel = 0;
        UINT32 Offset = 0;
        UINT32 Length = 0;
        int Poll;

        Poll = WifiPollOnePacket(&Channel, &Offset, &Length);
        if (Poll < 0) {
            SdhciDelayMicroseconds(1000);
            continue;
        }
        if (Poll > 0) {
            SdhciDelayMicroseconds(1000);
            continue;
        }
        if (Channel == WIFI_SDPCM_EVENT_CHANNEL && Length > Offset + 28u) {
            /* BDC(4) + eth(24) → event */
            const UINT8 *Payload = gWifiSdpcmPacket + Offset + 4;
            if (Payload[12] == 0x88 && Payload[13] == 0x6c) {
                int Handled = WifiHandleEscanEvent(Payload + 24, Length - Offset - 4u - 24u);
                if (Handled == 2) {
                    return 0;
                }
            }
        }
    }
    return (gWifiScanEntryCount > 0) ? 0 : -1;
}

int WifiAssociateOpen(const char *Ssid) {
    UINT8 Buffer[36];
    UINT32 Length = 0;
    UINT32 Index;

    if (Ssid == 0) {
        return -1;
    }
    while (Ssid[Length] != 0 && Length < WIFI_SSID_MAXIMUM_BYTES) {
        Length++;
    }
    if (Length == 0) {
        return -1;
    }
    if (WifiFirmwareLoad() != 0 || WifiRadioBringUp() != 0) {
        return -1;
    }
    WifiMemorySet(Buffer, 0, sizeof(Buffer));
    Buffer[0] = (UINT8)(Length & 0xFFu);
    Buffer[1] = (UINT8)((Length >> 8) & 0xFFu);
    Buffer[2] = (UINT8)((Length >> 16) & 0xFFu);
    Buffer[3] = (UINT8)((Length >> 24) & 0xFFu);
    for (Index = 0; Index < Length; Index++) {
        Buffer[4u + Index] = (UINT8)Ssid[Index];
    }
    if (WifiIoctl(WIFI_SDPCM_SET, WIFI_WLC_SET_SSID, Buffer, 36u, 0) != 0) {
        return -1;
    }
    HalSerialBootMarkForce("Boot: Pi WIFI assoc open sent\n");
    return 0;
}
