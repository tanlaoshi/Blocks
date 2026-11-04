/*
 * ScanReportFinish.c — 扫完排版：AP 行 / summary / 零 AP 失败文案
 *
 * 自 ScanReport.c 拆出（行数债）；合并表状态仍在 ScanReport.c。
 */
#include "Wifi/ScanPrivate.h"

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

static void ScanPrintAccessPoints(void) {
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
    ScanPrintAccessPoints();
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
