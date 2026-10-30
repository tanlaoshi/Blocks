/*
 * WifiScanResult.c — 解析 SCANU_RESULT_IND：一帧 → 1 个传输 BSSID + 0..N 个非传输 BSSID
 *
 * 用户眼里：`wififw scan` 合并后的每行
 *   WiFi: scan result #1: SSID="xxx" BSSID=aa:bb:cc:dd:ee:ff freq=2412 rssi=-45 ch=1
 *   WiFi: scan result #2: SSID="guest" BSSID=… (mbssid idx=2)
 * 从哪读：AicScanResultParse（唯一解析入口，由 WifiScanReport.c 在收帧时调用）、
 *         AicScanResultPrintAp / AicScanResultPrintMbssidFailure（扫完后排版用）。
 * 别改：payload 是 802.11 管理帧本体；IE 起点 36（24 B 头 + 12 B 定长体）；
 *       解不出来（不是 Beacon/ProbeRsp、帧太短、IE 截断）返回 0，不崩、不猜。
 *       汇总行必须先于取证 dump（用户要在 dump 里一眼看到 SSID）。
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

/* SSID 里不可打印的字节换成 '.'（hidden SSID 长度 0 时打印空串） */
static void ScanCopySsid(const UINT8 *Body, UINT32 BodyLength, char *Text, UINT32 TextSize) {
    UINT32 CopyLength = BodyLength;
    UINT32 Index;

    if (CopyLength > TextSize - 1u) {
        CopyLength = TextSize - 1u;
    }
    for (Index = 0; Index < CopyLength; Index++) {
        UINT8 Ch = Body[Index];

        Text[Index] = (Ch >= AIC_SSID_PRINTABLE_MIN && Ch <= AIC_SSID_PRINTABLE_MAX)
                          ? (char)Ch
                          : '.';
    }
    Text[CopyLength] = 0;
}

/*
 * 从 IE 链解 SSID(0) 与 DS Parameter(3)；Multiple BSSID(0x47) 由调用方再走一遍。
 * 起点是 36 而不是 24：rwnx_msg_rx.c:662-663、linux_5.10/net/wireless/scan.c:2309。
 * 截断就停：后面不可信。
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
            ScanCopySsid(&Frame[Body], ElementLength, SsidText, SsidTextSize);
            SsidFound = 1;
        } else if (ElementId == AIC_IE_DS_PARAMETER && ElementLength >= 1u) {
            *OutChannel = Frame[Body];
        }
        Index = Body + ElementLength;
    }
}

/*
 * 非传输 BSSID 的地址派生（**最后一个字节 A5**）：
 *   参考 BSSID 的 A0..A5；n = MaxBSSID Indicator；B = A5 mod 2^n
 *   A5(i) = A5 - B + ((B + i) mod 2^n)
 * 出处：IEEE 802.11ax-2021 Multiple BSSID 集合的 BSSID(i) 派生式；
 *       linux_5.10/include/net/cfg80211.h:6125 cfg80211_gen_new_bssid()。
 * MaxBssid 已在调用点限在 1..8，1u << 8 用 32 位算不会溢出。
 */
static void ScanDeriveBssid(const UINT8 *TxBssid, UINT8 MaxBssid, UINT8 Index, UINT8 *Out) {
    UINT32 Mask = (1u << (UINT32)MaxBssid) - 1u;
    UINT32 Base = (UINT32)TxBssid[AIC_SCAN_MAC_LENGTH - 1u];
    UINT32 Derived = (Base & ~Mask) | (((Base & Mask) + (UINT32)Index) & Mask);
    UINT32 k;

    for (k = 0; k < AIC_SCAN_MAC_LENGTH - 1u; k++) {
        Out[k] = TxBssid[k];
    }
    Out[AIC_SCAN_MAC_LENGTH - 1u] = (UINT8)Derived;
}

/*
 * 解析一个 Nontransmitted BSSID Profile 子元素体（它自己又是一条 IE 链）。
 * 第一个元素必须是 Non-TX BSSID Capability(83,len=2)；否则按 scan.c:2101 判无效。
 * 返回 0 = 可派生（index 合法），-1 = 解析失败；失败位置写 OutFailOffset。
 * SSID 缺失不算致命：保留空 SSID 并记错误位置，地址仍可派生。
 */
static int ScanParseProfileIes(const UINT8 *Frame, UINT32 Body, UINT32 BodyLength,
                               UINT8 MaxBssid, char *SsidText, UINT32 SsidTextSize,
                               UINT8 *OutIndex, UINT16 *OutFailOffset) {
    UINT32 Index = Body;
    UINT32 End = Body + BodyLength;
    int SsidFound = 0;
    int First = 1;

    SsidText[0] = 0;
    *OutIndex = 0;
    *OutFailOffset = AIC_SCAN_MBSSID_ERROR_NONE;
    while (Index + AIC_IE_HEADER_LENGTH <= End) {
        UINT8 ElementId = Frame[Index];
        UINT8 ElementLength = Frame[Index + 1u];
        UINT32 ElementBody = Index + AIC_IE_HEADER_LENGTH;

        if (ElementBody + ElementLength > End) {
            *OutFailOffset = (UINT16)Index;
            return -1;
        }
        if (First != 0) {
            if (ElementId != AIC_IE_NON_TX_BSSID_CAP ||
                ElementLength != AIC_IE_NON_TX_BSSID_CAP_LENGTH) {
                *OutFailOffset = (UINT16)Index;
                return -1;
            }
            First = 0;
        }
        if (ElementId == AIC_IE_SSID && SsidFound == 0) {
            ScanCopySsid(&Frame[ElementBody], ElementLength, SsidText, SsidTextSize);
            SsidFound = 1;
        } else if (ElementId == AIC_IE_MULTI_BSSID_INDEX && ElementLength >= 1u) {
            *OutIndex = Frame[ElementBody];
        }
        Index = ElementBody + ElementLength;
    }
    if (Index != End) {
        *OutFailOffset = (UINT16)Index; /* 尾部剩 1 B：链不完整 */
    }
    if (*OutIndex < AIC_MBSSID_INDEX_MIN ||
        (UINT32)*OutIndex >= (1u << (UINT32)MaxBssid)) {
        *OutFailOffset = (UINT16)Body;
        return -1;
    }
    if (SsidFound == 0 && *OutFailOffset == AIC_SCAN_MBSSID_ERROR_NONE) {
        *OutFailOffset = (UINT16)Index; /* 地址能派生，但 SSID 只能留空 */
    }
    return 0;
}

/*
 * 处理一个 profile 子元素：成功派生就追加到 Entries[Count]，返回新的 Count。
 * 失败只记 Out[0].MbssidErrorOffset（保留传输 BSSID 的现状），不抛、不崩。
 */
static UINT32 ScanAppendOneProfile(const UINT8 *Frame, UINT32 Position, UINT8 SubLength,
                                   UINT8 MaxBssid, AIC_SCAN_AP *Entries, UINT32 Count) {
    UINT32 SubBody = Position + AIC_IE_HEADER_LENGTH;
    char SsidText[AIC_SCAN_SSID_TEXT_MAX];
    UINT8 Index = 0;
    UINT16 FailOffset = AIC_SCAN_MBSSID_ERROR_NONE;
    AIC_SCAN_AP *Entry;
    UINT32 k;

    if (SubLength < AIC_MBSSID_PROFILE_MIN) {
        if (Entries[0].MbssidErrorOffset == AIC_SCAN_MBSSID_ERROR_NONE) {
            Entries[0].MbssidErrorOffset = (UINT16)Position;
        }
        return Count;
    }
    if (ScanParseProfileIes(Frame, SubBody, SubLength, MaxBssid, SsidText,
                            (UINT32)sizeof(SsidText), &Index, &FailOffset) != 0) {
        if (Entries[0].MbssidErrorOffset == AIC_SCAN_MBSSID_ERROR_NONE) {
            Entries[0].MbssidErrorOffset = FailOffset;
        }
        return Count;
    }
    Entry = &Entries[Count];
    Entry->Frequency = Entries[0].Frequency;
    Entry->Rssi = Entries[0].Rssi;
    Entry->Channel = Entries[0].Channel;
    Entry->MbssidIndex = Index;
    Entry->MbssidErrorOffset = FailOffset;
    for (k = 0; k < AIC_SCAN_SSID_TEXT_MAX; k++) {
        Entry->Ssid[k] = SsidText[k];
    }
    ScanDeriveBssid(Entries[0].Bssid, MaxBssid, Index, Entry->Bssid);
    return Count + 1u;
}

/*
 * 走 Multiple BSSID 元素的 Optional Subelements，为每个非传输 profile 生成一个 AP。
 * Entries[0] 是传输 BSSID；成功派生一个就追加一个，返回条目数。
 * 解析失败只记在 Out[0].MbssidErrorOffset，继续看下一个 profile；绝不崩。
 */
static UINT32 ScanAppendProfiles(const UINT8 *Frame, UINT32 MbssidOffset, UINT32 MbssidLength,
                                 AIC_SCAN_AP *Entries, UINT32 MaxEntries, UINT32 Count) {
    UINT32 Body = MbssidOffset + AIC_IE_HEADER_LENGTH;
    UINT32 End = Body + MbssidLength;
    UINT32 Position;
    UINT8 MaxBssid;
    int ProfileSeen = 0;

    if (Count >= MaxEntries) {
        return Count;
    }
    if (MbssidLength < AIC_MBSSID_INDICATOR_LENGTH) {
        Entries[0].MbssidErrorOffset = (UINT16)MbssidOffset;
        return Count;
    }
    MaxBssid = Frame[Body];
    if (MaxBssid == 0u || MaxBssid > AIC_MBSSID_MAX_BITS) {
        Entries[0].MbssidErrorOffset = (UINT16)Body;
        return Count;
    }
    Position = Body + AIC_MBSSID_INDICATOR_LENGTH;
    while (Position + AIC_IE_HEADER_LENGTH <= End && Count < MaxEntries) {
        UINT8 SubId = Frame[Position];
        UINT8 SubLength = Frame[Position + 1u];
        UINT32 SubBody = Position + AIC_IE_HEADER_LENGTH;

        if (SubBody + SubLength > End) {
            Entries[0].MbssidErrorOffset = (UINT16)Position;
            break;
        }
        if (SubId == 0u) {
            ProfileSeen = 1;
            Count = ScanAppendOneProfile(Frame, Position, SubLength, MaxBssid, Entries, Count);
        }
        Position = SubBody + SubLength;
    }
    if (ProfileSeen == 0 && Entries[0].MbssidErrorOffset == AIC_SCAN_MBSSID_ERROR_NONE) {
        Entries[0].MbssidErrorOffset = (UINT16)(Body + AIC_MBSSID_INDICATOR_LENGTH);
    }
    return Count;
}

/*
 * 唯一解析入口：把一条 SCANU_RESULT_IND 翻成 0..MaxEntries 个 AP。
 * 返回 0 = 不是 Beacon/ProbeRsp 或帧太短，调用方按“未解析帧”计数。
 */
UINT32 AicScanResultParse(const UINT8 *Frame, UINT32 FrameLength, UINT32 ParamLength,
                          AIC_SCAN_AP *Entries, UINT32 MaxEntries) {
    const AIC_SCANU_RESULT *Result;
    const UINT8 *Payload;
    UINT32 Available;
    UINT32 Declared;
    UINT32 Length;
    UINT32 Type;
    UINT32 Subtype;
    UINT32 Channel = 0;
    UINT32 Index;
    UINT32 Count;
    UINT32 k;

    if (MaxEntries == 0u || FrameLength < AIC_E2A_PARAM_OFF + AIC_SCANU_RESULT_HEADER ||
        ParamLength < AIC_SCANU_RESULT_HEADER) {
        return 0;
    }
    Result = (const AIC_SCANU_RESULT *)(Frame + AIC_E2A_PARAM_OFF);
    Payload = (const UINT8 *)Result->Payload;
    Available = ParamLength - AIC_SCANU_RESULT_HEADER;
    Declared = Result->Length;
    Length = Declared < Available ? Declared : Available;
    Type = Result->FrameControl & AIC_DOT11_TYPE_MASK;
    Subtype = (Result->FrameControl >> AIC_DOT11_SUBTYPE_SHIFT) & AIC_DOT11_SUBTYPE_MASK;
    if (Type != AIC_DOT11_TYPE_MANAGEMENT ||
        (Subtype != AIC_DOT11_SUBTYPE_BEACON &&
         Subtype != AIC_DOT11_SUBTYPE_PROBE_RESPONSE) ||
        Length < AIC_DOT11_IE_OFFSET) {
        return 0;
    }
    Entries[0].Frequency = Result->CenterFrequency;
    Entries[0].Rssi = Result->Rssi;
    Entries[0].MbssidIndex = 0;
    Entries[0].MbssidErrorOffset = AIC_SCAN_MBSSID_ERROR_NONE;
    for (k = 0; k < AIC_SCAN_MAC_LENGTH; k++) {
        Entries[0].Bssid[k] = Payload[AIC_DOT11_ADDRESS3_OFFSET + k];
    }
    ScanParseIes(Payload, Length, Entries[0].Ssid, (UINT32)sizeof(Entries[0].Ssid), &Channel);
    if (Channel == 0u) {
        Channel = ScanFrequencyToChannel(Result->CenterFrequency);
    }
    Entries[0].Channel = (UINT8)Channel;
    Count = 1;
    Index = AIC_DOT11_IE_OFFSET;
    while (Index + AIC_IE_HEADER_LENGTH <= Length) {
        UINT8 ElementId = Payload[Index];
        UINT8 ElementLength = Payload[Index + 1u];
        UINT32 Body = Index + AIC_IE_HEADER_LENGTH;

        if (Body + ElementLength > Length) {
            break;
        }
        if (ElementId == AIC_IE_MULTIPLE_BSSID && Count < MaxEntries) {
            Count = ScanAppendProfiles(Payload, Index, ElementLength, Entries, MaxEntries, Count);
        }
        Index = Body + ElementLength;
    }
    return Count;
}

void AicScanResultPrintAp(const AIC_SCAN_AP *Entry, UINT32 Index) {
    char AddressText[AIC_SCAN_MAC_TEXT_MAX];

    AicScanFormatAddress(Entry->Bssid, AddressText);
    SdhciMark("WiFi: scan result #");
    SdhciMarkDecimal("", Index);
    SdhciMark(": SSID=\"");
    SdhciMark(Entry->Ssid);
    SdhciMark("\" BSSID=");
    SdhciMark(AddressText);
    SdhciMark(" freq=");
    SdhciMarkDecimal("", (UINT32)Entry->Frequency);
    SdhciMark(" rssi=");
    AicScanMarkSigned((INT32)Entry->Rssi);
    SdhciMark(" ch=");
    SdhciMarkDecimal("", (UINT32)Entry->Channel);
    if (Entry->MbssidIndex != 0u) {
        SdhciMark(" (mbssid idx=");
        SdhciMarkDecimal("", (UINT32)Entry->MbssidIndex);
        SdhciMark(")");
    }
    SdhciMark("\n");
}

void AicScanResultPrintMbssidFailure(const AIC_SCAN_AP *Entry) {
    char AddressText[AIC_SCAN_MAC_TEXT_MAX];

    AicScanFormatAddress(Entry->Bssid, AddressText);
    SdhciMark("WiFi: mbssid: parse failed at off=");
    SdhciMarkHex("", (UINT32)Entry->MbssidErrorOffset, 4);
    SdhciMark(" BSSID=");
    SdhciMark(AddressText);
    SdhciMark(" (SSID kept empty)\n");
}
