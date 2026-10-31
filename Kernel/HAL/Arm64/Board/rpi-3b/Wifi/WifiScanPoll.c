/*
 * WifiScanPoll.c — F2 收包 + escan 事件解析（辅助 WifiScan.c）
 */
#include "Wifi/Private.h"

static UINT32 gWifiScanPollEmptyCount;
static UINT32 gWifiScanPollErrorCount;
static UINT32 gWifiScanEventPacketCount;
static UINT32 gWifiScanEscanPartialCount;
static UINT32 gWifiScanEtherTypeOkCount;
static UINT32 gWifiScanFirstBdcDataOffset;
static UINT32 gWifiScanFirstEventTypeAt2;
static UINT32 gWifiScanFirstEventTypeAt4;
static UINT32 gWifiScanFirstEventStatusAt6;
static UINT32 gWifiScanFirstEventStatusAt8;
static UINT32 gWifiScanFirstEventLength;
static int gWifiScanEventSampleTaken;

#define WIFI_EVENT_ESCAN_RESULT 69u
#define WIFI_ESCAN_STATUS_PARTIAL 8u
#define WIFI_ESCAN_STATUS_SUCCESS 0u

UINT32 WifiScanPollEmptyCount(void) {
    return gWifiScanPollEmptyCount;
}

UINT32 WifiScanPollErrorCount(void) {
    return gWifiScanPollErrorCount;
}

UINT32 WifiScanEventPacketCount(void) {
    return gWifiScanEventPacketCount;
}

UINT32 WifiScanEscanPartialCount(void) {
    return gWifiScanEscanPartialCount;
}

UINT32 WifiScanEtherTypeOkCount(void) {
    return gWifiScanEtherTypeOkCount;
}

UINT32 WifiScanFirstBdcDataOffset(void) {
    return gWifiScanFirstBdcDataOffset;
}

UINT32 WifiScanFirstEventTypeAt2(void) {
    return gWifiScanFirstEventTypeAt2;
}

UINT32 WifiScanFirstEventTypeAt4(void) {
    return gWifiScanFirstEventTypeAt4;
}

UINT32 WifiScanFirstEventStatusAt6(void) {
    return gWifiScanFirstEventStatusAt6;
}

UINT32 WifiScanFirstEventStatusAt8(void) {
    return gWifiScanFirstEventStatusAt8;
}

UINT32 WifiScanFirstEventLength(void) {
    return gWifiScanFirstEventLength;
}

void WifiScanPollCountersReset(void) {
    gWifiScanPollEmptyCount = 0;
    gWifiScanPollErrorCount = 0;
    gWifiScanEventPacketCount = 0;
    gWifiScanEscanPartialCount = 0;
    gWifiScanEtherTypeOkCount = 0;
    gWifiScanFirstBdcDataOffset = 0;
    gWifiScanFirstEventTypeAt2 = 0;
    gWifiScanFirstEventTypeAt4 = 0;
    gWifiScanFirstEventStatusAt6 = 0;
    gWifiScanFirstEventStatusAt8 = 0;
    gWifiScanFirstEventLength = 0;
    gWifiScanEventSampleTaken = 0;
}

void WifiScanPollEmptyBump(void) {
    gWifiScanPollEmptyCount++;
}

void WifiScanPollErrorBump(void) {
    gWifiScanPollErrorCount++;
}

void WifiScanEventPacketBump(void) {
    gWifiScanEventPacketCount++;
}

static UINT32 WifiGetBig32(const UINT8 *Source) {
    return ((UINT32)Source[0] << 24) | ((UINT32)Source[1] << 16) | ((UINT32)Source[2] << 8) |
           (UINT32)Source[3];
}

void WifiScanEventSampleNote(UINT8 BdcDataOffset, UINT16 EtherType, const UINT8 *Event,
                             UINT32 EventLength) {
    if (EtherType != 0x886Cu) {
        return;
    }
    gWifiScanEtherTypeOkCount++;
    if (gWifiScanEventSampleTaken || Event == 0 || EventLength < 12u) {
        return;
    }
    gWifiScanEventSampleTaken = 1;
    gWifiScanFirstBdcDataOffset = BdcDataOffset;
    gWifiScanFirstEventLength = EventLength;
    gWifiScanFirstEventTypeAt2 = WifiGetBig32(Event + 2);
    gWifiScanFirstEventTypeAt4 = WifiGetBig32(Event + 4);
    gWifiScanFirstEventStatusAt6 = WifiGetBig32(Event + 6);
    gWifiScanFirstEventStatusAt8 = WifiGetBig32(Event + 8);
}

static void WifiMemoryCopy(void *Destination, const void *Source, UINT32 Bytes) {
    UINT8 *Out = (UINT8 *)Destination;
    const UINT8 *In = (const UINT8 *)Source;
    UINT32 Index;
    for (Index = 0; Index < Bytes; Index++) {
        Out[Index] = In[Index];
    }
}

static UINT16 WifiGetLittle16(const UINT8 *Source) {
    return (UINT16)Source[0] | ((UINT16)Source[1] << 8);
}

static INT16 WifiGetLittleSigned16(const UINT8 *Source) {
    return (INT16)WifiGetLittle16(Source);
}

int WifiPollOnePacket(UINT8 *OutChannel, UINT32 *OutOffset, UINT32 *OutLength) {
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

int WifiHandleEscanEvent(const UINT8 *Event, UINT32 Length) {
    UINT32 EventType;
    UINT32 Status;
    const UINT8 *Scan;
    UINT8 SsidLength;
    UINT32 Index;

    /*
     * Event = eth payload+24。0x14e 眼睛：type_at4=69 status_at8=8（PARTIAL），
     * type_at2=0 —— 线缆含 version@_0，与 cyw43_async_event 一致；0x14d 的 +2 否证。
     */
    if (Length < 48u + 92u) {
        return 0;
    }
    EventType = WifiGetBig32(Event + 4);
    Status = WifiGetBig32(Event + 8);
    if (EventType != WIFI_EVENT_ESCAN_RESULT) {
        return 0;
    }
    if (Status == WIFI_ESCAN_STATUS_SUCCESS) {
        return 2; /* 扫完 */
    }
    if (Status != WIFI_ESCAN_STATUS_PARTIAL) {
        return 0;
    }
    gWifiScanEscanPartialCount++;
    Scan = Event + 48;
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
