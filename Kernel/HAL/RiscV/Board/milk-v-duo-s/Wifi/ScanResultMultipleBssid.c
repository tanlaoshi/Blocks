/*
 * ScanResultMultipleBssid.c — Multiple BSSID IE → 非传输 profile 派生
 *
 * 自 ScanResult.c 拆出（行数债）；行为不变。
 */
#include "Wifi/ScanPrivate.h"

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

static void ScanDeriveBssid(const UINT8 *TransmitBssid, UINT8 MaxBssid, UINT8 Index,
                            UINT8 *Out) {
    UINT32 Mask = (1u << (UINT32)MaxBssid) - 1u;
    UINT32 Base = (UINT32)TransmitBssid[AIC_SCAN_MAC_LENGTH - 1u];
    UINT32 Derived = (Base & ~Mask) | (((Base & Mask) + (UINT32)Index) & Mask);
    UINT32 ByteIndex;

    for (ByteIndex = 0; ByteIndex < AIC_SCAN_MAC_LENGTH - 1u; ByteIndex++) {
        Out[ByteIndex] = TransmitBssid[ByteIndex];
    }
    Out[AIC_SCAN_MAC_LENGTH - 1u] = (UINT8)Derived;
}

static int ScanParseProfileInformationElements(const UINT8 *Frame, UINT32 Body, UINT32 BodyLength,
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
        *OutFailOffset = (UINT16)Index;
    }
    if (*OutIndex < AIC_MBSSID_INDEX_MIN ||
        (UINT32)*OutIndex >= (1u << (UINT32)MaxBssid)) {
        *OutFailOffset = (UINT16)Body;
        return -1;
    }
    if (SsidFound == 0 && *OutFailOffset == AIC_SCAN_MBSSID_ERROR_NONE) {
        *OutFailOffset = (UINT16)Index;
    }
    return 0;
}

static UINT32 ScanAppendOneProfile(const UINT8 *Frame, UINT32 Position, UINT8 SubLength,
                                   UINT8 MaxBssid, AIC_SCAN_AP *Entries, UINT32 Count) {
    UINT32 SubBody = Position + AIC_IE_HEADER_LENGTH;
    char SsidText[AIC_SCAN_SSID_TEXT_MAX];
    UINT8 Index = 0;
    UINT16 FailOffset = AIC_SCAN_MBSSID_ERROR_NONE;
    AIC_SCAN_AP *Entry;
    UINT32 ByteIndex;

    if (SubLength < AIC_MBSSID_PROFILE_MIN) {
        if (Entries[0].MbssidErrorOffset == AIC_SCAN_MBSSID_ERROR_NONE) {
            Entries[0].MbssidErrorOffset = (UINT16)Position;
        }
        return Count;
    }
    if (ScanParseProfileInformationElements(Frame, SubBody, SubLength, MaxBssid, SsidText,
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
    Entry->RsnGroupSuite = Entries[0].RsnGroupSuite;
    Entry->MbssidIndex = Index;
    Entry->MbssidErrorOffset = FailOffset;
    for (ByteIndex = 0; ByteIndex < AIC_SCAN_SSID_TEXT_MAX; ByteIndex++) {
        Entry->Ssid[ByteIndex] = SsidText[ByteIndex];
    }
    ScanDeriveBssid(Entries[0].Bssid, MaxBssid, Index, Entry->Bssid);
    return Count + 1u;
}

UINT32 ScanAppendProfiles(const UINT8 *Frame, UINT32 MbssidOffset, UINT32 MbssidLength,
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
