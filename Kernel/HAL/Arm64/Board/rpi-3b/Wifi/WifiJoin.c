/*
 * WifiJoin.c — pi-wifi-7 首刀：WPA2-PSK 关联（对表 cyw43_ll_wifi_join）
 * 扫到课网后发 wsec/pmk/wpa_auth + SET_SSID，等 LINK/SET_SSID，落 WIFI-JOIN.LOG。
 */
#include "Wifi/Private.h"

#define WIFI_WLC_SET_INFRA           20u
#define WIFI_WLC_SET_AUTH            22u
#define WIFI_WLC_SET_WSEC            134u
#define WIFI_WLC_SET_WPA_AUTH        165u
#define WIFI_WLC_SET_WSEC_PMK        268u
#define WIFI_WSEC_AES                0x4u
#define WIFI_WPA2_AUTH_PSK           0x80u
#define WIFI_AUTH_OPEN               0u
#define WIFI_MFP_CAPABLE             1u
#define WIFI_PASSWORD_MAXIMUM_BYTES  64u
#define WIFI_EVENT_SET_SSID          0u
#define WIFI_EVENT_LINK              16u
#define WIFI_EVENT_PSK_SUP           46u
#define WIFI_JOIN_POLL_ATTEMPTS      8000u
#define WIFI_CLASSROOM_SSID          "Apple"
#define WIFI_CLASSROOM_PASSWORD      "Tank1989"

static const char *gWifiJoinStatusText = "join-idle";
static UINT32 gWifiJoinEventType;
static UINT32 gWifiJoinEventStatus;

void WifiJoinPersistLog(const char *Status, UINT32 EventType, UINT32 EventStatus);

static void WifiJoinMemorySet(void *Destination, UINT8 Value, UINT32 Bytes) {
    UINT8 *Cursor = (UINT8 *)Destination;
    UINT32 Index;
    for (Index = 0; Index < Bytes; Index++) {
        Cursor[Index] = Value;
    }
}

static void WifiJoinMemoryCopy(void *Destination, const void *Source, UINT32 Bytes) {
    UINT8 *Out = (UINT8 *)Destination;
    const UINT8 *In = (const UINT8 *)Source;
    UINT32 Index;
    for (Index = 0; Index < Bytes; Index++) {
        Out[Index] = In[Index];
    }
}

static void WifiJoinWriteLittle16(UINT8 *Destination, UINT16 Value) {
    Destination[0] = (UINT8)(Value & 0xFFu);
    Destination[1] = (UINT8)((Value >> 8) & 0xFFu);
}

static void WifiJoinWriteLittle32(UINT8 *Destination, UINT32 Value) {
    Destination[0] = (UINT8)(Value & 0xFFu);
    Destination[1] = (UINT8)((Value >> 8) & 0xFFu);
    Destination[2] = (UINT8)((Value >> 16) & 0xFFu);
    Destination[3] = (UINT8)((Value >> 24) & 0xFFu);
}

static UINT32 WifiJoinGetBig32(const UINT8 *Source) {
    return ((UINT32)Source[0] << 24) | ((UINT32)Source[1] << 16) | ((UINT32)Source[2] << 8) |
           (UINT32)Source[3];
}

static int WifiJoinStringEqual(const char *Left, const char *Right) {
    UINT32 Index = 0;
    if (Left == 0 || Right == 0) {
        return 0;
    }
    while (Left[Index] != 0 && Right[Index] != 0) {
        if (Left[Index] != Right[Index]) {
            return 0;
        }
        Index++;
    }
    return (Left[Index] == 0 && Right[Index] == 0) ? 1 : 0;
}

static int WifiJoinIoctlSetUnsigned32(UINT32 Command, UINT32 Value) {
    UINT8 Buffer[4];
    WifiJoinWriteLittle32(Buffer, Value);
    return WifiIoctl(WIFI_SDPCM_SET, Command, Buffer, 4u, 0);
}

static int WifiJoinSetVariableUnsigned32(const char *Name, UINT32 Value) {
    UINT8 Buffer[4];
    WifiJoinWriteLittle32(Buffer, Value);
    return WifiIoctlSetVariable(Name, Buffer, 4u);
}

static int WifiJoinSetBsscfgUnsigned32(const char *Name, UINT32 Index, UINT32 Value) {
    UINT8 Buffer[64];
    UINT32 NameBytes = 0;
    UINT32 Cursor;

    while (Name[NameBytes] != 0) {
        NameBytes++;
    }
    if (NameBytes + 1u + 8u > sizeof(Buffer)) {
        return -1;
    }
    for (Cursor = 0; Cursor < NameBytes; Cursor++) {
        Buffer[Cursor] = (UINT8)Name[Cursor];
    }
    Buffer[NameBytes] = 0;
    WifiJoinWriteLittle32(Buffer + NameBytes + 1u, Index);
    WifiJoinWriteLittle32(Buffer + NameBytes + 1u + 4u, Value);
    return WifiIoctl(WIFI_SDPCM_SET, WIFI_WLC_SET_VAR, Buffer, NameBytes + 1u + 8u, 0);
}

static int WifiJoinConfigureWpa2Psk(const char *Password, UINT32 PasswordBytes) {
    UINT8 PmkBuffer[4u + WIFI_PASSWORD_MAXIMUM_BYTES];

    if (WifiJoinIoctlSetUnsigned32(WIFI_WLC_SET_WSEC, WIFI_WSEC_AES) != 0) {
        return -1;
    }
    if (WifiJoinSetBsscfgUnsigned32("bsscfg:sup_wpa", 0u, 1u) != 0) {
        return -1;
    }
    if (WifiJoinSetBsscfgUnsigned32("bsscfg:sup_wpa2_eapver", 0u, 0xFFFFFFFFu) != 0) {
        return -1;
    }
    WifiJoinMemorySet(PmkBuffer, 0, sizeof(PmkBuffer));
    WifiJoinWriteLittle16(PmkBuffer, (UINT16)PasswordBytes);
    WifiJoinWriteLittle16(PmkBuffer + 2, 1u);
    WifiJoinMemoryCopy(PmkBuffer + 4, Password, PasswordBytes);
    SdhciDelayMicroseconds(2000);
    if (WifiIoctl(WIFI_SDPCM_SET, WIFI_WLC_SET_WSEC_PMK, PmkBuffer,
                  4u + WIFI_PASSWORD_MAXIMUM_BYTES, 0) != 0) {
        return -1;
    }
    if (WifiJoinIoctlSetUnsigned32(WIFI_WLC_SET_INFRA, 1u) != 0) {
        return -1;
    }
    if (WifiJoinIoctlSetUnsigned32(WIFI_WLC_SET_AUTH, WIFI_AUTH_OPEN) != 0) {
        return -1;
    }
    (void)WifiJoinSetVariableUnsigned32("mfp", WIFI_MFP_CAPABLE);
    if (WifiJoinIoctlSetUnsigned32(WIFI_WLC_SET_WPA_AUTH, WIFI_WPA2_AUTH_PSK) != 0) {
        return -1;
    }
    return 0;
}

static int WifiJoinSendSetSsid(const char *Ssid, UINT32 SsidBytes) {
    UINT8 Buffer[36];
    UINT32 Index;

    WifiJoinMemorySet(Buffer, 0, sizeof(Buffer));
    WifiJoinWriteLittle32(Buffer, SsidBytes);
    for (Index = 0; Index < SsidBytes; Index++) {
        Buffer[4u + Index] = (UINT8)Ssid[Index];
    }
    return WifiIoctl(WIFI_SDPCM_SET, WIFI_WLC_SET_SSID, Buffer, 36u, 0);
}

static int WifiJoinHandleAssociateEvent(const UINT8 *Event, UINT32 EventLength) {
    UINT32 EventType;
    UINT32 Status;

    if (EventLength < 12u) {
        return 0;
    }
    EventType = WifiJoinGetBig32(Event + 4);
    Status = WifiJoinGetBig32(Event + 8);
    gWifiJoinEventType = EventType;
    gWifiJoinEventStatus = Status;
    if (EventType == WIFI_EVENT_LINK && Status == 0u) {
        return 1;
    }
    if (EventType == WIFI_EVENT_SET_SSID && Status == 0u) {
        return 1;
    }
    if (EventType == WIFI_EVENT_PSK_SUP && Status == 0u) {
        return 1;
    }
    if (EventType == WIFI_EVENT_SET_SSID && Status != 0u) {
        return -1;
    }
    return 0;
}

static int WifiJoinWaitAssociated(void) {
    UINT32 Attempt;

    for (Attempt = 0; Attempt < WIFI_JOIN_POLL_ATTEMPTS; Attempt++) {
        UINT8 Channel = 0;
        UINT32 Offset = 0;
        UINT32 Length = 0;
        int Poll;

        if ((Attempt & 0xFFu) == 0u) {
            (void)WifiSdioSleepControlEnsureAwake();
        }
        Poll = WifiPollOnePacket(&Channel, &Offset, &Length);
        if (Poll != 0) {
            SdhciDelayMicroseconds(1000);
            continue;
        }
        if (Channel == WIFI_SDPCM_EVENT_CHANNEL && Length > Offset + 28u) {
            const UINT8 *Bdc = gWifiSdpcmPacket + Offset;
            UINT32 BdcSkip = 4u + ((UINT32)Bdc[3] << 2);
            const UINT8 *Payload;
            UINT16 EtherType;
            int Handled;

            if (Offset + BdcSkip + 24u >= Length) {
                continue;
            }
            Payload = gWifiSdpcmPacket + Offset + BdcSkip;
            EtherType = (UINT16)(((UINT16)Payload[12] << 8) | Payload[13]);
            if (EtherType != 0x886Cu) {
                continue;
            }
            Handled = WifiJoinHandleAssociateEvent(Payload + 24, Length - Offset - BdcSkip - 24u);
            if (Handled > 0) {
                return 0;
            }
            if (Handled < 0) {
                return -1;
            }
        }
    }
    return -1;
}

static int WifiJoinAssociateWpa2Psk(const char *Ssid, const char *Password) {
    UINT32 SsidBytes = 0;
    UINT32 PasswordBytes = 0;

    while (Ssid[SsidBytes] != 0 && SsidBytes < WIFI_SSID_MAXIMUM_BYTES) {
        SsidBytes++;
    }
    while (Password[PasswordBytes] != 0 && PasswordBytes < WIFI_PASSWORD_MAXIMUM_BYTES) {
        PasswordBytes++;
    }
    if (SsidBytes == 0u || PasswordBytes == 0u) {
        gWifiJoinStatusText = "join-arg";
        return -1;
    }
    if (WifiJoinConfigureWpa2Psk(Password, PasswordBytes) != 0) {
        gWifiJoinStatusText = "join-cfg-fail";
        HalSerialBootMarkForce("Boot: Pi WIFI join cfg fail\n");
        return -1;
    }
    if (WifiJoinSendSetSsid(Ssid, SsidBytes) != 0) {
        gWifiJoinStatusText = "join-ssid-fail";
        HalSerialBootMarkForce("Boot: Pi WIFI join SET_SSID fail\n");
        return -1;
    }
    HalSerialBootMarkForce("Boot: Pi WIFI join SET_SSID ok, wait link\n");
    if (WifiJoinWaitAssociated() != 0) {
        gWifiJoinStatusText = "join-wait-fail";
        HalSerialBootMarkForce("Boot: Pi WIFI join wait fail\n");
        return -1;
    }
    gWifiJoinStatusText = "join-ok";
    HalSerialBootMarkForce("Boot: Pi WIFI join ok\n");
    return 0;
}

int WifiJoinAfterScan(void) {
    UINT32 Index;
    int Found = 0;

    gWifiJoinEventType = 0;
    gWifiJoinEventStatus = 0;
    gWifiJoinStatusText = "join-skip";
    for (Index = 0; Index < gWifiScanEntryCount; Index++) {
        if (WifiJoinStringEqual(gWifiScanEntries[Index].Ssid, WIFI_CLASSROOM_SSID)) {
            Found = 1;
            break;
        }
    }
    if (!Found) {
        WifiJoinPersistLog("join-no-apple", 0u, 0u);
        return 0;
    }
    if (WifiJoinAssociateWpa2Psk(WIFI_CLASSROOM_SSID, WIFI_CLASSROOM_PASSWORD) != 0) {
        WifiJoinPersistLog(gWifiJoinStatusText, gWifiJoinEventType, gWifiJoinEventStatus);
        return -1;
    }
    WifiJoinPersistLog(gWifiJoinStatusText, gWifiJoinEventType, gWifiJoinEventStatus);
    /* 0x15a：DHCP Discover + DATA→lwIP 眼睛 */
    (void)WifiNetProbeAfterJoin();
    return 0;
}
