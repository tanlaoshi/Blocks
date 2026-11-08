/*
 * ScanReport.c — 多轮扫描按 BSSID 合并（收帧路径只解析，不打印）
 *
 * 排版见 ScanReportFinish.c。行为不变。
 */
#include "Wifi/ScanPrivate.h"
#include "CString.h"

AIC_SCAN_REPORT_ENTRY sAicScanEntries[AIC_SCAN_AP_MAX];
UINT32 sAicScanEntryCount;
UINT32 sAicScanDropped;
AIC_SCAN_RECORD sAicScanRecords[AIC_SCAN_RESULT_KEEP_MAX];
UINT32 sAicScanRecordCount;
UINT8 sAicScanRecordDumped[AIC_SCAN_RESULT_KEEP_MAX];
UINT32 sAicScanRoundsRequested;
UINT32 sAicScanRoundsDone;
UINT32 sAicScanCurrentRound;
UINT32 sAicScanRoundCounts[AIC_SCAN_ROUNDS_MAX];
UINT32 sAicScanTotalFrames;
UINT32 sAicScanTotalElapsed;
UINT32 sAicScanUnparsed;
int sAicScanVerbose;
int sAicScanAnyDone;
int sAicScanLastEnd;

static int ScanAddressEqual(const UINT8 *Left, const UINT8 *Right) {
    UINT32 Index;

    for (Index = 0; Index < AIC_SCAN_MAC_LENGTH; Index++) {
        if (Left[Index] != Right[Index]) {
            return 0;
        }
    }
    return 1;
}

static void ScanCopyText(char *Destination, const char *Source, UINT32 Size) {
    UINT32 Index;

    for (Index = 0; Index < Size; Index++) {
        Destination[Index] = Source[Index];
        if (Source[Index] == 0) {
            return;
        }
    }
    Destination[Size - 1u] = 0;
}

static AIC_SCAN_REPORT_ENTRY *ScanFindEntry(const UINT8 *Bssid) {
    UINT32 Index;

    for (Index = 0; Index < sAicScanEntryCount; Index++) {
        if (ScanAddressEqual(sAicScanEntries[Index].Ap.Bssid, Bssid) != 0) {
            return &sAicScanEntries[Index];
        }
    }
    return 0;
}

int AicScanReportFind(const char *Ssid, const UINT8 *Bssid, AIC_SCAN_AP *Out) {
    const AIC_SCAN_REPORT_ENTRY *Best = 0;
    UINT32 Index;

    if (Ssid == 0 || Out == 0) {
        return -1;
    }
    for (Index = 0; Index < sAicScanEntryCount; Index++) {
        const AIC_SCAN_REPORT_ENTRY *Entry = &sAicScanEntries[Index];

        if (StringEqual(Entry->Ap.Ssid, Ssid) == 0) {
            continue;
        }
        if (Bssid != 0) {
            if (ScanAddressEqual(Entry->Ap.Bssid, Bssid) != 0) {
                *Out = Entry->Ap;
                return 0;
            }
            continue;
        }
        if (Best == 0 || Entry->Ap.Rssi > Best->Ap.Rssi) {
            Best = Entry;
        }
    }
    if (Best == 0) {
        return -1;
    }
    *Out = Best->Ap;
    return 0;
}

UINT32 AicScanReportCount(void) {
    return sAicScanEntryCount;
}

int AicScanReportCopyAt(UINT32 Index, AIC_SCAN_AP *Out) {
    if (Out == 0 || Index >= sAicScanEntryCount) {
        return -1;
    }
    *Out = sAicScanEntries[Index].Ap;
    return 0;
}

static int ScanNeedsFrame(const AIC_SCAN_AP *Ap) {
    AIC_SCAN_REPORT_ENTRY *Entry = ScanFindEntry(Ap->Bssid);

    if (Entry == 0) {
        return sAicScanEntryCount < AIC_SCAN_AP_MAX ? 1 : 0;
    }
    if (Ap->Rssi > Entry->Ap.Rssi) {
        return 1;
    }
    if (Entry->Ap.Ssid[0] == 0 && Ap->Ssid[0] != 0) {
        return 1;
    }
    return 0;
}

static UINT16 ScanStoreFrame(const UINT8 *Frame, UINT32 FrameLength, UINT32 ParamLength) {
    AIC_SCAN_RECORD *Record;
    UINT32 CopyLength;

    if (sAicScanVerbose == 0 || sAicScanRecordCount >= AIC_SCAN_RESULT_KEEP_MAX) {
        return AIC_SCAN_RECORD_INDEX_NONE;
    }
    Record = &sAicScanRecords[sAicScanRecordCount];
    CopyLength = FrameLength > AIC_SCAN_FRAME_MAX ? AIC_SCAN_FRAME_MAX : FrameLength;
    Record->FrameLength = (UINT16)CopyLength;
    Record->ParamLength = (UINT16)ParamLength;
    (void)memcpy(Record->Frame, Frame, CopyLength);
    return (UINT16)sAicScanRecordCount++;
}

static AIC_SCAN_REPORT_ENTRY *ScanNewEntry(const AIC_SCAN_AP *Ap, UINT16 RecordIndex) {
    AIC_SCAN_REPORT_ENTRY *Entry;

    if (sAicScanEntryCount >= AIC_SCAN_AP_MAX) {
        sAicScanDropped++;
        return 0;
    }
    Entry = &sAicScanEntries[sAicScanEntryCount++];
    Entry->Ap = *Ap;
    Entry->RecordIndex = RecordIndex;
    Entry->LastRound = 0;
    return Entry;
}

static void ScanMerge(const AIC_SCAN_AP *Ap, UINT16 RecordIndex) {
    AIC_SCAN_REPORT_ENTRY *Entry = ScanFindEntry(Ap->Bssid);

    if (Entry == 0) {
        Entry = ScanNewEntry(Ap, RecordIndex);
        if (Entry == 0) {
            return;
        }
    } else {
        if (Entry->Ap.Ssid[0] == 0 && Ap->Ssid[0] != 0) {
            ScanCopyText(Entry->Ap.Ssid, Ap->Ssid, (UINT32)sizeof(Entry->Ap.Ssid));
            if (RecordIndex != AIC_SCAN_RECORD_INDEX_NONE) {
                Entry->RecordIndex = RecordIndex;
            }
        }
        if (Ap->Rssi > Entry->Ap.Rssi) {
            Entry->Ap.Rssi = Ap->Rssi;
            Entry->Ap.Frequency = Ap->Frequency;
            Entry->Ap.Channel = Ap->Channel;
            if (RecordIndex != AIC_SCAN_RECORD_INDEX_NONE) {
                Entry->RecordIndex = RecordIndex;
            }
        }
        if (Entry->Ap.RsnGroupSuite == 0u && Ap->RsnGroupSuite != 0u) {
            Entry->Ap.RsnGroupSuite = Ap->RsnGroupSuite;
        }
        if (Ap->MbssidIndex != 0u) {
            Entry->Ap.MbssidIndex = Ap->MbssidIndex;
        }
    }
    if (Entry->LastRound != sAicScanCurrentRound) {
        Entry->LastRound = sAicScanCurrentRound;
        if (sAicScanCurrentRound >= 1u && sAicScanCurrentRound <= AIC_SCAN_ROUNDS_MAX) {
            sAicScanRoundCounts[sAicScanCurrentRound - 1u]++;
        }
    }
}

void AicScanReportReset(UINT32 Rounds, int Verbose) {
    UINT32 Index;

    if (Rounds == 0u) {
        Rounds = AIC_SCAN_ROUNDS_DEFAULT;
    }
    if (Rounds > AIC_SCAN_ROUNDS_MAX) {
        Rounds = AIC_SCAN_ROUNDS_MAX;
    }
    sAicScanRoundsRequested = Rounds;
    sAicScanRoundsDone = 0;
    sAicScanCurrentRound = 1;
    sAicScanEntryCount = 0;
    sAicScanDropped = 0;
    sAicScanRecordCount = 0;
    sAicScanTotalFrames = 0;
    sAicScanTotalElapsed = 0;
    sAicScanUnparsed = 0;
    sAicScanVerbose = Verbose;
    sAicScanAnyDone = 0;
    sAicScanLastEnd = AIC_SCAN_END_IDLE;
    for (Index = 0; Index < AIC_SCAN_ROUNDS_MAX; Index++) {
        sAicScanRoundCounts[Index] = 0;
    }
    for (Index = 0; Index < AIC_SCAN_RESULT_KEEP_MAX; Index++) {
        sAicScanRecordDumped[Index] = 0;
    }
}

void AicScanReportRound(UINT32 Round) {
    sAicScanCurrentRound = Round;
}

void AicScanReportClear(void) {
    AicScanReportReset(AIC_SCAN_ROUNDS_DEFAULT, AIC_SCAN_VERBOSE_OFF);
}

void AicScanReportAdd(const UINT8 *Frame, UINT32 FrameLength, UINT32 ParamLength) {
    AIC_SCAN_AP Parsed[AIC_SCAN_AP_PER_FRAME];
    UINT32 Count;
    UINT32 Index;
    UINT16 RecordIndex = AIC_SCAN_RECORD_INDEX_NONE;
    int NeedFrame = 0;

    Count = AicScanResultParse(Frame, FrameLength, ParamLength, Parsed, AIC_SCAN_AP_PER_FRAME);
    sAicScanTotalFrames++;
    if (Count == 0u) {
        sAicScanUnparsed++;
        return;
    }
    for (Index = 0; Index < Count; Index++) {
        if (ScanNeedsFrame(&Parsed[Index]) != 0) {
            NeedFrame = 1;
            break;
        }
    }
    if (NeedFrame != 0) {
        RecordIndex = ScanStoreFrame(Frame, FrameLength, ParamLength);
    }
    for (Index = 0; Index < Count; Index++) {
        ScanMerge(&Parsed[Index], RecordIndex);
    }
}

void AicScanReportRoundDone(UINT32 Elapsed, int Done, int End) {
    if (sAicScanRoundsDone < sAicScanRoundsRequested) {
        sAicScanRoundsDone++;
    }
    sAicScanTotalElapsed += Elapsed;
    if (Done != 0) {
        sAicScanAnyDone = 1;
    }
    sAicScanLastEnd = End;
}
