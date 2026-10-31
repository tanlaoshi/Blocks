/*
 * WifiScan.c — radio up / escan / 开放关联（pwifi-2）
 * 收包与 escan 事件：WifiScanPoll.c
 */
#include "Wifi/Private.h"

BOARD_WIFI_SCAN_ENTRY gWifiScanEntries[WIFI_SCAN_RESULT_MAXIMUM];
UINT32 gWifiScanEntryCount;
static int gWifiRadioUp;
static const char *gWifiScanFailStatusText;
static const char *gWifiRadioFailStatusText;

const char *WifiScanFailStatus(void) {
    return gWifiScanFailStatusText;
}

const char *WifiRadioFailStatus(void) {
    return gWifiRadioFailStatusText;
}

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

static UINT32 WifiReadLittle32(const UINT8 *Source) {
    return (UINT32)Source[0] | ((UINT32)Source[1] << 8) | ((UINT32)Source[2] << 16) |
           ((UINT32)Source[3] << 24);
}

/* 0x146：WLC_GET_UP；1=已 up，0=未 up，-1=ioctl 失败 */
static int WifiInterfaceGetUp(void) {
    UINT8 Buffer[4];
    UINT32 OutBytes = 0;

    WifiMemorySet(Buffer, 0, sizeof(Buffer));
    if (WifiIoctl(WIFI_SDPCM_GET, WIFI_WLC_GET_UP, Buffer, sizeof(Buffer), &OutBytes) != 0) {
        return -1;
    }
    return (WifiReadLittle32(Buffer) != 0u) ? 1 : 0;
}

int WifiRadioBringUp(void) {
    UINT8 Buffer[48];
    UINT32 Zero = 0;
    UINT32 One = 1;
    UINT32 Attempt;
    int UpState;

    if (!gWifiFirmwareLoaded) {
        gWifiRadioFailStatusText = "radio-fw";
        return -1;
    }
    if (gWifiRadioUp) {
        gWifiRadioFailStatusText = 0;
        return 0;
    }
    gWifiRadioFailStatusText = 0;

    /*
     * 跳过 country（0x149 否证：XX/ALL 皆拒；NVRAM 已 ccode=ALL）。
     * 0x14a：GET_UP 用 cmd 162（0x149 误用 6=DUMP → -23）。
     */
    HalSerialBootMarkForce("Boot: Pi WIFI skip country (nvram ccode)\n");

    WifiMemorySet(Buffer, 0, sizeof(Buffer));
    WifiMemoryCopy(Buffer, &Zero, 4);
    if (WifiIoctlSetVariable("bus:txglom", Buffer, 4u) != 0) {
        /* 非致命 */
    }
    WifiMemoryCopy(Buffer, &One, 4);
    (void)WifiIoctlSetVariable("apsta", Buffer, 4u);

    /* event_msgs：mask 先全开 0xff，再关掉噪声事件（对表 cyw43） */
    WifiMemorySet(Buffer, 0, sizeof(Buffer));
    WifiMemorySet(Buffer + 22, 0xFFu, 19u);
    Buffer[22 + (19u / 8u)] &= (UINT8) ~(1u << (19u % 8u)); /* roam */
    Buffer[22 + (20u / 8u)] &= (UINT8) ~(1u << (20u % 8u)); /* tx fail */
    Buffer[22 + (40u / 8u)] &= (UINT8) ~(1u << (40u % 8u)); /* radio */
    Buffer[22 + (44u / 8u)] &= (UINT8) ~(1u << (44u % 8u)); /* probe req */
    Buffer[22 + (54u / 8u)] &= (UINT8) ~(1u << (54u % 8u)); /* iface */
    Buffer[22 + (71u / 8u)] &= (UINT8) ~(1u << (71u % 8u)); /* probe resp */
    WifiMemoryCopy(Buffer, "bsscfg:event_msgs", 18); /* 含 '\\0' */
    if (WifiIoctl(WIFI_SDPCM_SET, WIFI_WLC_SET_VAR, Buffer, 18u + 4u + 19u, 0) != 0) {
        gWifiRadioFailStatusText = "event-msgs-fail";
        HalSerialBootMarkForce("Boot: Pi WIFI event_msgs fail\n");
        return -1;
    }
    SdhciDelayMicroseconds(50000);
    if (WifiIoctl(WIFI_SDPCM_SET, WIFI_WLC_UP, 0, 0, 0) != 0) {
        gWifiRadioFailStatusText = "up-fail";
        HalSerialBootMarkForce("Boot: Pi WIFI WLC_UP fail\n");
        return -1;
    }
    /* 等 GET_UP=1；中途再补一次 WLC_UP（防 NOTUP 竞态） */
    UpState = 0;
    for (Attempt = 0; Attempt < 50u; Attempt++) {
        SdhciDelayMicroseconds(20000);
        UpState = WifiInterfaceGetUp();
        if (UpState > 0) {
            break;
        }
        if (Attempt == 24u) {
            (void)WifiIoctl(WIFI_SDPCM_SET, WIFI_WLC_UP, 0, 0, 0);
        }
    }
    if (UpState <= 0) {
        gWifiRadioFailStatusText = "getup-fail";
        HalSerialBootMarkForce("Boot: Pi WIFI GET_UP still down\n");
        return -1;
    }
    gWifiRadioUp = 1;
    HalSerialBootMarkForce("Boot: Pi WIFI radio up\n");
    return 0;
}

int WifiScanAccessPoints(void) {
    /* 对表 cyw43_wifi_scan_options_t：sizeof=76（含 channel_list） */
    UINT8 Options[76];
    UINT32 Attempt;
    INT32 MinusOne = -1;
    UINT32 Version = 1;
    UINT16 Action = 1;

    gWifiScanEntryCount = 0;
    gWifiScanFailStatusText = 0;
    WifiScanPollCountersReset();
    if (WifiFirmwareLoad() != 0) {
        gWifiScanFailStatusText = "scan-fw";
        return -1;
    }
    if (WifiRadioBringUp() != 0) {
        gWifiScanFailStatusText = "scan-radio";
        return -1;
    }
    /* cyw43 poll 前 bus_sleep(false) ≡ KSO 保醒 */
    if (WifiSdioSleepControlEnsureAwake() != 0) {
        gWifiScanFailStatusText = "scan-kso";
        return -1;
    }

    WifiMemorySet(Options, 0, sizeof(Options));
    WifiMemoryCopy(Options + 0, &Version, 4);
    WifiMemoryCopy(Options + 4, &Action, 2);
    WifiMemorySet(Options + 44, 0xFF, 6); /* bssid */
    Options[50] = 2;                      /* bss_type ANY */
    WifiMemoryCopy(Options + 52, &MinusOne, 4);
    WifiMemoryCopy(Options + 56, &MinusOne, 4);
    WifiMemoryCopy(Options + 60, &MinusOne, 4);
    WifiMemoryCopy(Options + 64, &MinusOne, 4);
    /* channel_num@68=0；channel_list@72 已清零 */

    if (WifiIoctlSetVariable("escan", Options, sizeof(Options)) != 0) {
        gWifiScanFailStatusText = "escan-fail";
        HalSerialBootMarkForce("Boot: Pi WIFI escan ioctl fail\n");
        return -1;
    }

    for (Attempt = 0; Attempt < 8000u; Attempt++) {
        UINT8 Channel = 0;
        UINT32 Offset = 0;
        UINT32 Length = 0;
        int Poll;

        if ((Attempt & 0xFFu) == 0u) {
            (void)WifiSdioSleepControlEnsureAwake();
        }
        Poll = WifiPollOnePacket(&Channel, &Offset, &Length);
        if (Poll < 0) {
            WifiScanPollErrorBump();
            SdhciDelayMicroseconds(1000);
            continue;
        }
        if (Poll > 0) {
            WifiScanPollEmptyBump();
            SdhciDelayMicroseconds(1000);
            continue;
        }
        if (Channel == WIFI_SDPCM_EVENT_CHANNEL && Length > Offset + 28u) {
            /* BDC(4) + eth(24) → event；data_offset 单位是 4B 字 */
            const UINT8 *Bdc = gWifiSdpcmPacket + Offset;
            UINT8 BdcDataOffset = Bdc[3];
            UINT32 BdcSkip = 4u + ((UINT32)BdcDataOffset << 2);
            const UINT8 *Payload = gWifiSdpcmPacket + Offset + BdcSkip;
            UINT16 EtherType;
            UINT32 EventLength;

            if (Offset + BdcSkip + 24u >= Length) {
                WifiScanPollErrorBump();
                continue;
            }
            EtherType = (UINT16)(((UINT16)Payload[12] << 8) | Payload[13]);
            EventLength = Length - Offset - BdcSkip - 24u;
            WifiScanEventPacketBump();
            WifiScanEventSampleNote(BdcDataOffset, EtherType, Payload + 24, EventLength);
            if (EtherType == 0x886Cu) {
                int Handled = WifiHandleEscanEvent(Payload + 24, EventLength);
                if (Handled == 2) {
                    gWifiScanFailStatusText = 0;
                    return 0;
                }
            }
        }
    }
    if (gWifiScanEntryCount > 0) {
        gWifiScanFailStatusText = 0;
        /* 0x150：wifi-7 首刀——扫到 Apple 则 WPA2 关联并落 WIFI-JOIN.LOG */
        (void)WifiJoinAfterScan();
        return 0;
    }
    gWifiScanFailStatusText = "scan-empty";
    HalSerialBootMarkForce("Boot: Pi WIFI scan empty (timeout)\n");
    return -1;
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
