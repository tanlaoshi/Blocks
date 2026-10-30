/*
 * WifiScanReport.c — 多轮扫描按 BSSID 合并，扫完再一次性排版
 *
 * 用户眼里：`wififw scan` / `scan verbose` 先安静收完 N 轮，最后一次性给出
 *   WiFi: scan result #N: SSID="…" …          ← 每条合并结果一行，dump 在其后
 *   WiFi: scan summary: 4 APs from 3 rounds (rounds: 3,2,3) done=idle
 * 从哪读：AicScanReportReset / Round / Add / RoundDone / Finish。
 * 别改：接收路径只解析 + 最多 memcpy 一帧，不打印 —— 115200 下几十行 dump 要几百毫秒，
 *       期间不轮询 SDIO，固件侧的结果/测量帧会积压甚至被丢（真机 verbose 那次
 *       survey 缺了 2422..2442）。打印只发生在全部轮次收帧结束之后。
 * 轮间不清缓存：Reset 只在第一轮之前调用一次；Round 只改当前轮号，Entries 一直保留。
 * 合并规则：同一 BSSID 取最强 RSSI 的那次；SSID 取第一次非空的那次；派生地址/idx
 *       取非传输 profile 的那次。每轮只统计“本轮首次见到的 BSSID”（rounds: 3,2,3）。
 * 为什么单独一个文件：WifiScan.c 只管编排收帧，这里只管“合并 + 排版”。
 * 预算：32 个 AP 表项 + 16 个 verbose 整帧槽（25 KiB 静态 BSS），不做动态分配。
 */
#include "WifiScanPrivate.h"
#include "CString.h"

/* 合并表项：解析结果 + 本轮首次见到的轮号 + verbose 最强帧槽号 */
typedef struct {
    AIC_SCAN_AP Ap;
    UINT16 RecordIndex; /* AIC_SCAN_RECORD_INDEX_NONE = 没有可 dump 的帧 */
    UINT32 LastRound;
} AIC_SCAN_REPORT_ENTRY;

static AIC_SCAN_REPORT_ENTRY sAicScanEntries[AIC_SCAN_AP_MAX];
static UINT32 sAicScanEntryCount;
static UINT32 sAicScanDropped;
static AIC_SCAN_RECORD sAicScanRecords[AIC_SCAN_RESULT_KEEP_MAX];
static UINT32 sAicScanRecordCount;
static UINT8 sAicScanRecordDumped[AIC_SCAN_RESULT_KEEP_MAX];
static UINT32 sAicScanRoundsRequested;
static UINT32 sAicScanRoundsDone;
static UINT32 sAicScanCurrentRound;
static UINT32 sAicScanRoundCounts[AIC_SCAN_ROUNDS_MAX];
static UINT32 sAicScanTotalFrames;
static UINT32 sAicScanTotalElapsed;
static UINT32 sAicScanUnparsed;
static int sAicScanVerbose;
static int sAicScanAnyDone;
static int sAicScanLastEnd;

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

/* 只有新 AP、RSSI 更强、或补上非空 SSID 才值得留一份整帧给 verbose */
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

    if (sAicScanVerbose == 0 ||
        sAicScanRecordCount >= AIC_SCAN_RESULT_KEEP_MAX) {
        return AIC_SCAN_RECORD_INDEX_NONE;
    }
    Record = &sAicScanRecords[sAicScanRecordCount];
    CopyLength = FrameLength > AIC_SCAN_FRAME_MAX ? AIC_SCAN_FRAME_MAX : FrameLength;
    Record->FrameLength = (UINT16)CopyLength;
    Record->ParamLength = (UINT16)ParamLength;
    (void)memcpy(Record->Frame, Frame, CopyLength);
    return (UINT16)sAicScanRecordCount++;
}

/* 新 AP 只追加；表满就丢展示，但本轮/总数仍按真实帧走（summary 会说明） */
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
                Entry->RecordIndex = RecordIndex; /* dump 跟着“有 SSID 的那帧”走 */
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

static const char *ScanEndName(int End) {
    if (End == AIC_SCAN_END_DONE) {
        return "0x1001";
    }
    if (End == AIC_SCAN_END_IDLE) {
        return "idle";
    }
    return "budget";
}

static const char *ScanEndTag(void) {
    if (sAicScanAnyDone != 0) {
        return "done=0x1001";
    }
    if (sAicScanLastEnd == AIC_SCAN_END_BUDGET) {
        return "done=budget";
    }
    return "done=idle";
}

static void ScanPrintSummary(void) {
    UINT32 Index;

    SdhciMark("WiFi: scan summary: ");
    SdhciMarkDecimal("", sAicScanEntryCount);
    SdhciMark(" APs from ");
    SdhciMarkDecimal("", sAicScanRoundsDone);
    SdhciMark(" rounds (rounds: ");
    for (Index = 0; Index < sAicScanRoundsDone; Index++) {
        if (Index > 0u) {
            SdhciMark(",");
        }
        SdhciMarkDecimal("", sAicScanRoundCounts[Index]);
    }
    SdhciMark(") ");
    SdhciMark(ScanEndTag());
    SdhciMark("\n");
}

static void ScanPrintAps(void) {
    UINT32 Index;

    for (Index = 0; Index < sAicScanEntryCount; Index++) {
        AIC_SCAN_REPORT_ENTRY *Entry = &sAicScanEntries[Index];
        UINT16 RecordIndex = Entry->RecordIndex;

        AicScanResultPrintAp(&Entry->Ap, Index + 1u);
        if (Entry->Ap.MbssidErrorOffset != AIC_SCAN_MBSSID_ERROR_NONE) {
            AicScanResultPrintMbssidFailure(&Entry->Ap);
        }
        if (sAicScanVerbose != 0 && RecordIndex != AIC_SCAN_RECORD_INDEX_NONE &&
            RecordIndex < sAicScanRecordCount &&
            sAicScanRecordDumped[RecordIndex] == 0u) {
            const AIC_SCAN_RECORD *Record = &sAicScanRecords[RecordIndex];

            sAicScanRecordDumped[RecordIndex] = 1;
            AicScanDumpVerbose(Record->Frame, Record->FrameLength, Record->ParamLength,
                               Index + 1u);
        }
    }
}

/* 三种 0 AP 失败必须一眼分开：① done 了但没 BSS ② 没 done ③ 连帧都没有 */
static int ScanPrintZeroFailure(void) {
    if (sAicScanAnyDone != 0) {
        SdhciMark("WiFi: scan result FAILED: done=0x1001, 0 APs");
        SdhciMark(" (firmware finished; no BSS on the 13 channels)\n");
        return -1;
    }
    if (sAicScanTotalFrames == 0u) {
        SdhciMark("WiFi: scan result FAILED: no frames at all in ");
        SdhciMarkDecimal("", sAicScanTotalElapsed);
        SdhciMark("ms (SDIO rx / channel path)\n");
        return -1;
    }
    SdhciMark("WiFi: scan result FAILED: no 0x1001 in ");
    SdhciMarkDecimal("", sAicScanTotalElapsed);
    SdhciMark("ms (");
    SdhciMark(ScanEndName(sAicScanLastEnd));
    SdhciMark("), 0 APs (timing / firmware unfinished)\n");
    return -1;
}

int AicScanReportFinish(void) {
    if (sAicScanAnyDone != 0) {
        SdhciMark("WiFi: scan done ind (0x1001)\n");
    }
    ScanPrintAps();
    if (sAicScanDropped != 0u) {
        SdhciMark("WiFi: scan: ");
        SdhciMarkDecimal("", sAicScanDropped);
        SdhciMark(" APs not shown (table ");
        SdhciMarkDecimal("", AIC_SCAN_AP_MAX);
        SdhciMark(")\n");
    }
    if (sAicScanUnparsed != 0u) {
        SdhciMark("WiFi: scan: ");
        SdhciMarkDecimal("", sAicScanUnparsed);
        SdhciMark(" frames not Beacon/ProbeRsp\n");
    }
    ScanPrintSummary();
    if (sAicScanEntryCount != 0u) {
        return 0;
    }
    return ScanPrintZeroFailure();
}
