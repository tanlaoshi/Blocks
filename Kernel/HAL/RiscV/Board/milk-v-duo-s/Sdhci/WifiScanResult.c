/*
 * WifiScanResult.c — 解析 SCANU_RESULT_IND 并逐条打到串口
 *
 * 用户眼里：`wififw scan` 边收边打的
 *   WiFi: scan result #1: SSID="xxx" BSSID=aa:bb:cc:dd:ee:ff freq=2412 rssi=-45 ch=1
 * 从哪读：AicScanResultPrint（唯一对外入口）。
 * 别改：payload 是 802.11 管理帧本体；SSID/DS Parameter 都从 IE 里解，
 *       解不出来（不是 Beacon/ProbeRsp、帧太短、IE 截断）就打 raw，不崩。
 * 来源：struct scanu_result_ind（lmac_msg.h:1627）、802.11 管理帧固定头 24 B + IE。
 */
#include "WifiScanPrivate.h"

void AicScanMarkSigned(INT32 Value) {
    if (Value < 0) {
        SdhciMark("-");
        SdhciMarkDecimal("", (UINT32)(-Value));
        return;
    }
    SdhciMarkDecimal("", (UINT32)Value);
}

/* 小写十六进制（HalSerialFormatHex 也是小写），用于拼 aa:bb:.. 的 MAC */
static char ScanHexDigit(UINT8 Value) {
    UINT32 Nibble = (UINT32)(Value & 0x0Fu);

    if (Nibble < 10u) {
        return (char)('0' + Nibble);
    }
    return (char)('a' + (Nibble - 10u));
}

void AicScanFormatAddress(const UINT8 *Address, char *Text) {
    UINT32 Index;
    UINT32 Position = 0;

    for (Index = 0; Index < AIC_SCAN_MAC_LENGTH; Index++) {
        if (Index > 0u) {
            Text[Position++] = ':';
        }
        Text[Position++] = ScanHexDigit((UINT8)(Address[Index] >> 4));
        Text[Position++] = ScanHexDigit(Address[Index]);
    }
    Text[Position] = 0;
}

/* 2.4G：freq = 2412 + 5*(n-1) ⇒ n = (freq-2412)/5 + 1；范围外返回 0 */
static UINT32 ScanFrequencyToChannel(UINT16 Frequency) {
    if (Frequency < AIC_SCAN_CHANNEL_2G4_FIRST || Frequency > AIC_SCAN_CHANNEL_2G4_LAST) {
        return 0;
    }
    return (UINT32)(Frequency - AIC_SCAN_CHANNEL_2G4_FIRST) / AIC_SCAN_CHANNEL_2G4_STEP + 1u;
}

/*
 * 从 IE 链解 SSID(0) 和 DS Parameter(3)。
 *
 * 起点是 36 而不是 24：Beacon/ProbeResp 在 24 B 定长头之后还有 12 B 定长体
 * （timestamp 8 + beacon_interval 2 + capability 2），IE 从 36 起。
 * 厂商就是这么交给上层的：rwnx_msg_rx.c:662 `ie = mgmt->u.probe_resp.variable`、
 * :663 `ielen = len - offsetof(struct ieee80211_mgmt, u.probe_resp.variable)`；
 * 内核 cfg80211 同款（linux_5.10/net/wireless/scan.c:2309/2339/2340）。
 * 以前从 24 起 ⇒ 第一个“IE”是 timestamp 的前 2 B（固件留 0 ⇒ id=0 len=0），
 * SSID 永远是空串，DS Parameter 靠随机字节撞 —— 真机 4 条全空、一条 ch 与 freq 打架。
 *
 * SSID 里不可打印的字节换成 '.'（hidden SSID 长度 0 时打印空串）。
 * IE 长度越界就停：截断帧后面不可信。
 */
static void ScanParseIes(const UINT8 *Frame, UINT32 FrameLength, char *SsidText,
                         UINT32 SsidTextSize, UINT32 *OutChannel) {
    UINT32 Index = AIC_DOT11_IE_OFFSET;
    int SsidFound = 0;

    SsidText[0] = 0;
    *OutChannel = 0;
    while (Index + AIC_IE_HEADER_LENGTH <= FrameLength) {
        UINT8 ElementId = Frame[Index];
        UINT8 ElementLength = Frame[Index + 1u];
        UINT32 Body = Index + AIC_IE_HEADER_LENGTH;

        if (Body + ElementLength > FrameLength) {
            break;
        }
        if (ElementId == AIC_IE_SSID && SsidFound == 0) {
            UINT32 CopyLength = ElementLength;
            UINT32 k;

            if (CopyLength > SsidTextSize - 1u) {
                CopyLength = SsidTextSize - 1u;
            }
            for (k = 0; k < CopyLength; k++) {
                UINT8 Ch = Frame[Body + k];

                SsidText[k] = (Ch >= AIC_SSID_PRINTABLE_MIN && Ch <= AIC_SSID_PRINTABLE_MAX)
                                  ? (char)Ch
                                  : '.';
            }
            SsidText[CopyLength] = 0;
            SsidFound = 1;
        } else if (ElementId == AIC_IE_DS_PARAMETER && ElementLength >= 1u) {
            *OutChannel = Frame[Body];
        }
        Index = Body + ElementLength;
    }
}

static void ScanPrintRaw(UINT32 Index, const UINT8 *Payload, UINT32 Length) {
    UINT32 Count = Length > AIC_SCAN_RAW_HEAD_BYTES ? AIC_SCAN_RAW_HEAD_BYTES : Length;
    UINT32 k;

    SdhciMark("WiFi: scan result #");
    SdhciMarkDecimal("", Index);
    SdhciMark(": raw len=");
    SdhciMarkDecimal("", Length);
    SdhciMark(" head=");
    for (k = 0; k < Count; k++) {
        SdhciMarkHex(" ", (UINT32)Payload[k], 2);
    }
    SdhciMark("\n");
}

static void ScanPrintResultLine(UINT32 Index, const char *Ssid, const char *Bssid,
                                UINT16 Frequency, INT8 Rssi, UINT32 Channel) {
    SdhciMark("WiFi: scan result #");
    SdhciMarkDecimal("", Index);
    SdhciMark(": SSID=\"");
    SdhciMark(Ssid);
    SdhciMark("\" BSSID=");
    SdhciMark(Bssid);
    SdhciMark(" freq=");
    SdhciMarkDecimal("", (UINT32)Frequency);
    SdhciMark(" rssi=");
    AicScanMarkSigned((INT32)Rssi);
    SdhciMark(" ch=");
    SdhciMarkDecimal("", Channel);
    SdhciMark("\n");
}

void AicScanResultPrint(const UINT8 *Frame, UINT32 FrameLength, UINT32 ParamLength, UINT32 Index,
                        int Verbose) {
    const AIC_SCANU_RESULT *Result;
    const UINT8 *Payload;
    UINT32 Available;
    UINT32 Declared;
    UINT32 Length;
    UINT32 Type;
    UINT32 Subtype;
    UINT32 Channel;
    char SsidText[AIC_SCAN_SSID_TEXT_MAX];
    char AddressText[AIC_SCAN_MAC_TEXT_MAX];

    /* verbose：先原样取证（含短帧），再走解析；顺序固定，真机输出就是对判据 */
    if (Verbose != AIC_SCAN_VERBOSE_OFF) {
        AicScanDumpVerbose(Frame, FrameLength, ParamLength, Index);
    }
    if (FrameLength < AIC_E2A_PARAM_OFF + AIC_SCANU_RESULT_HEADER ||
        ParamLength < AIC_SCANU_RESULT_HEADER) {
        SdhciMark("WiFi: scan result #");
        SdhciMarkDecimal("", Index);
        SdhciMarkHex(": short frame plen=", ParamLength, 4);
        SdhciMark("\n");
        return;
    }
    Result = (const AIC_SCANU_RESULT *)(Frame + AIC_E2A_PARAM_OFF);
    Payload = (const UINT8 *)Result->Payload;
    Available = ParamLength - AIC_SCANU_RESULT_HEADER;
    Declared = Result->Length;
    Length = Declared < Available ? Declared : Available;
    if (Declared > Available) {
        SdhciMarkHex("WiFi: scan result frame truncated: declared=", Declared, 4);
        SdhciMarkHex(" available=", Available, 4);
        SdhciMark("\n");
    }
    Type = Result->FrameControl & AIC_DOT11_TYPE_MASK;
    Subtype = (Result->FrameControl >> AIC_DOT11_SUBTYPE_SHIFT) & AIC_DOT11_SUBTYPE_MASK;
    if (Type != AIC_DOT11_TYPE_MANAGEMENT ||
        (Subtype != AIC_DOT11_SUBTYPE_BEACON &&
         Subtype != AIC_DOT11_SUBTYPE_PROBE_RESPONSE) ||
        Length < AIC_DOT11_IE_OFFSET) {
        ScanPrintRaw(Index, Payload, Length);
        return;
    }
    ScanParseIes(Payload, Length, SsidText, (UINT32)sizeof(SsidText), &Channel);
    if (Channel == 0u) {
        Channel = ScanFrequencyToChannel(Result->CenterFrequency);
    }
    AicScanFormatAddress(&Payload[AIC_DOT11_ADDRESS3_OFFSET], AddressText);
    ScanPrintResultLine(Index, SsidText, AddressText, Result->CenterFrequency, Result->Rssi,
                        Channel);
}
