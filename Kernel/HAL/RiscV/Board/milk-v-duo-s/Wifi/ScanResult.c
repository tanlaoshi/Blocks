/*
 * ScanResult.c — 解析 SCANU_RESULT_IND：一帧 → 传输 BSSID（+ Multiple BSSID 见姊妹文件）
 *
 * Multiple BSSID 派生在 ScanResultMultipleBssid.c。行为不变。
 */
#include "Wifi/ScanPrivate.h"

void AicScanMarkSigned(INT32 Value) {
    if (Value < 0) {
        SdhciMark("-");
        SdhciMarkDecimal("", (UINT32)(-Value));
        return;
    }
    SdhciMarkDecimal("", (UINT32)Value);
}

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

static UINT32 ScanFrequencyToChannel(UINT16 Frequency) {
    if (Frequency < AIC_SCAN_CHANNEL_2G4_FIRST || Frequency > AIC_SCAN_CHANNEL_2G4_LAST) {
        return 0;
    }
    return (UINT32)(Frequency - AIC_SCAN_CHANNEL_2G4_FIRST) / AIC_SCAN_CHANNEL_2G4_STEP + 1u;
}

static void ScanCopySsid(const UINT8 *Body, UINT32 BodyLength, char *Text, UINT32 TextSize) {
    UINT32 CopyLength = BodyLength;
    UINT32 Index;

    if (CopyLength > TextSize - 1u) {
        CopyLength = TextSize - 1u;
    }
    for (Index = 0; Index < CopyLength; Index++) {
        UINT8 Character = Body[Index];

        Text[Index] = (Character >= AIC_SSID_PRINTABLE_MIN && Character <= AIC_SSID_PRINTABLE_MAX)
                          ? (char)Character
                          : '.';
    }
    Text[CopyLength] = 0;
}

static UINT8 ScanParseRsnGroupSuite(const UINT8 *Body, UINT8 ElementLength) {
    if (ElementLength < AIC_RSN_GROUP_SUITE_MIN) {
        return 0;
    }
    if (Body[AIC_RSN_VERSION_LENGTH] != AIC_RSN_SUITE_OUI0 ||
        Body[AIC_RSN_VERSION_LENGTH + 1u] != AIC_RSN_SUITE_OUI1 ||
        Body[AIC_RSN_VERSION_LENGTH + 2u] != AIC_RSN_SUITE_OUI2) {
        return 0;
    }
    return Body[AIC_RSN_VERSION_LENGTH + 3u];
}

static void ScanParseInformationElements(const UINT8 *Frame, UINT32 FrameLength, char *SsidText,
                                         UINT32 SsidTextSize, UINT32 *OutChannel,
                                         UINT8 *OutRsnGroupSuite) {
    UINT32 Index = AIC_DOT11_IE_OFFSET;
    int SsidFound = 0;

    SsidText[0] = 0;
    *OutChannel = 0;
    *OutRsnGroupSuite = 0;
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
        } else if (ElementId == AIC_IE_RSN && *OutRsnGroupSuite == 0u) {
            *OutRsnGroupSuite = ScanParseRsnGroupSuite(&Frame[Body], ElementLength);
        }
        Index = Body + ElementLength;
    }
}

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
    UINT32 ByteIndex;

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
    Entries[0].RsnGroupSuite = 0;
    for (ByteIndex = 0; ByteIndex < AIC_SCAN_MAC_LENGTH; ByteIndex++) {
        Entries[0].Bssid[ByteIndex] = Payload[AIC_DOT11_ADDRESS3_OFFSET + ByteIndex];
    }
    ScanParseInformationElements(Payload, Length, Entries[0].Ssid,
                                 (UINT32)sizeof(Entries[0].Ssid), &Channel,
                                 &Entries[0].RsnGroupSuite);
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
