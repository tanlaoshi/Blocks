/*
 * Scan.c — `wififw scan [repeat N]`：编排 SCANU_START + 收集 + dry
 *
 * VIF / 共用缓冲见 ScanVirtualInterface.c。行为不变。
 */
#include "Wifi/ScanPrivate.h"

static int ScanStart(SDHCI_HOST *Host, UINT8 VifIndex, UINT16 *OutAckId) {
    UINT32 *Request = ScanRequestBuffer();
    UINT32 Length = AicScanuStartBuild((UINT8 *)Request, VifIndex);
    UINT32 ParamLength = 0;
    UINT16 Id = 0;

    ScanPrintTransmit("scanu start", Length, " (2.4G 13 ch, ssid_cnt=0)");
    if (AicSendMessageEx(Host, (UINT16)AIC_SCANU_START_REQ, (UINT16)AIC_TASK_SCANU, Request,
                         (UINT16)Length) != 0) {
        SdhciMark("WiFi: scanu start send FAILED (SDIO tx)\n");
        return -1;
    }
    if (ScanWaitConfirm(Host, (UINT16)AIC_SCANU_START_CFM, (UINT16)AIC_SCANU_START_CFM_ADDITIONAL,
                        &Id, &ParamLength) != 0) {
        SdhciMark("WiFi: scanu start: no 0x1001/0x1009 in 2000ms\n");
        return -1;
    }
    if (Id == AIC_SCANU_START_CFM) {
        const AIC_SCANU_START_CONFIRM *Confirm =
            (const AIC_SCANU_START_CONFIRM *)(ScanFrameBuffer() + AIC_E2A_PARAM_OFF);

        if (ParamLength < AIC_SCANU_START_CFM_LENGTH) {
            SdhciMarkHex("WiFi: scanu start cfm too short plen=", ParamLength, 4);
            SdhciMark("\n");
            return -1;
        }
        if (Confirm->Status != 0u) {
            SdhciMarkHex("WiFi: scanu start cfm status=", Confirm->Status, 2);
            SdhciMark(" (firmware refused the channel list)\n");
            return -1;
        }
    }
    *OutAckId = Id;
    SdhciMark("WiFi: scanu start cfm ok");
    if (Id == AIC_SCANU_START_CFM_ADDITIONAL) {
        SdhciMark(" (0x1009)");
    }
    SdhciMark("\n");
    return 0;
}

static int ScanCollect(SDHCI_HOST *Host, UINT16 AckId, int Verbose, UINT32 Round, UINT32 Rounds) {
    UINT8 *Frame = ScanFrameBuffer();
    UINT32 Started = ScanNowMilliseconds();
    UINT32 Idle = 0;
    int Done = AckId == (UINT16)AIC_SCANU_START_CFM;
    int End = AIC_SCAN_END_BUDGET;

    SdhciMark("WiFi: scan collecting round ");
    SdhciMarkDecimal("", Round);
    SdhciMark("/");
    SdhciMarkDecimal("", Rounds);
    SdhciMark(" (budget ");
    SdhciMarkDecimal("", AIC_SCAN_ROUND_BUDGET_MS);
    SdhciMark("ms, idle ");
    SdhciMarkDecimal("", AIC_SCAN_IDLE_LIMIT * AIC_SCAN_RX_POLL_MS);
    SdhciMark(Verbose != AIC_SCAN_VERBOSE_OFF ? "ms, verbose queued)\n" : "ms)\n");
    while ((UINT32)(ScanNowMilliseconds() - Started) < AIC_SCAN_ROUND_BUDGET_MS) {
        UINT16 Id = 0;
        UINT32 FrameLength = 0;
        UINT32 ParamLength = 0;

        if (AicReceiveFrameTimeout(Host, Frame, AIC_SCAN_FRAME_MAX, &Id, &FrameLength,
                                   &ParamLength, 1u, AIC_SCAN_RX_POLL_MS) != 0) {
            if (Done != 0) {
                End = AIC_SCAN_END_DONE;
                break;
            }
            Idle++;
            if (Idle >= AIC_SCAN_IDLE_LIMIT) {
                End = AIC_SCAN_END_IDLE;
                break;
            }
            continue;
        }
        Idle = 0;
        if (Id == (UINT16)AIC_SCANU_RESULT_IND) {
            AicScanReportAdd(Frame, FrameLength, ParamLength);
        } else if (Id == (UINT16)AIC_SCANU_START_CFM) {
            Done = 1;
        } else {
            AicScanReportIgnored(Id, ParamLength, Frame, Verbose);
        }
    }
    if (Done != 0) {
        End = AIC_SCAN_END_DONE;
    }
    AicScanReportRoundDone((UINT32)(ScanNowMilliseconds() - Started), Done, End);
    return 0;
}

static int ScanDry(void) {
    UINT32 *Request = ScanRequestBuffer();
    UINT32 Length;

    if (AicScanLayoutCheck() != 0) {
        return -1;
    }
    SdhciMark("WiFi: scan dry: build only, nothing sent\n");
    Length = AicMmAddIfBuild((UINT8 *)Request);
    ScanPrintTransmit("add if", Length, "");
    ScanDumpBytes((const UINT8 *)Request, Length);
    Length = AicScanuStartBuild((UINT8 *)Request, (UINT8)AIC_SCAN_DRY_VIF_INDEX);
    ScanPrintTransmit("scanu start", Length, " (2.4G 13 ch, ssid_cnt=0)");
    ScanDumpBytes((const UINT8 *)Request, Length);
    SdhciMark("WiFi: scan dry ok (2 built, 0 sent)\n");
    return 0;
}

static int ScanOneRound(SDHCI_HOST *Host, UINT8 VifIndex, int Verbose, UINT32 Round,
                        UINT32 Rounds) {
    UINT16 AckId = 0;

    if (ScanStart(Host, VifIndex, &AckId) != 0) {
        return -1;
    }
    return ScanCollect(Host, AckId, Verbose, Round, Rounds);
}

static int ScanRun(SDHCI_HOST *Host, int Verbose, UINT32 Rounds) {
    UINT8 VifIndex = 0;
    UINT32 Round;
    int Reused = ScanVirtualInterfaceIsCached();

    if (AicScanLayoutCheck() != 0) {
        return -1;
    }
    SdhciMark("WiFi: scan rounds=");
    SdhciMarkDecimal("", Rounds);
    SdhciMark(" (BSSID merge, add_if once, no cache clear between rounds)\n");
    AicScanReportReset(Rounds, Verbose);
    if (ScanVirtualInterfaceAcquire(Host, &VifIndex) != 0) {
        SdhciMark("WiFi: scan FAILED at step 1/3 (add_if)\n");
        return -1;
    }
    for (Round = 1; Round <= Rounds; Round++) {
        AicScanReportRound(Round);
        if (ScanOneRound(Host, VifIndex, Verbose, Round, Rounds) == 0) {
            continue;
        }
        if (Round != 1u) {
            SdhciMark("WiFi: scan: round ");
            SdhciMarkDecimal("", Round);
            SdhciMark(" scanu_start failed, keeping previous rounds\n");
            break;
        }
        if (Reused == 0) {
            SdhciMark("WiFi: scan FAILED at step 2/3 (scanu_start, round 1)\n");
            return -1;
        }
        SdhciMark("WiFi: scan: cached vif rejected, re-add once\n");
        ScanVirtualInterfaceInvalidate();
        if (ScanVirtualInterfaceAcquire(Host, &VifIndex) != 0) {
            SdhciMark("WiFi: scan FAILED at step 2/3 (scanu_start, round 1)\n");
            return -1;
        }
        AicScanReportRound(1);
        if (ScanOneRound(Host, VifIndex, Verbose, 1, Rounds) != 0) {
            SdhciMark("WiFi: scan FAILED at step 2/3 (scanu_start, round 1)\n");
            return -1;
        }
    }
    if (AicScanReportFinish() != 0) {
        SdhciMark("WiFi: scan FAILED at step 3/3 (receive results)\n");
        return -1;
    }
    return 0;
}

int SdhciWifiScan(int Mode, UINT32 Rounds) {
    SDHCI_HOST *Host;
    int Verbose = Mode == AIC_SCAN_MODE_VERBOSE ? AIC_SCAN_VERBOSE_ON : AIC_SCAN_VERBOSE_OFF;

    if (Rounds == 0u) {
        Rounds = AIC_SCAN_ROUNDS_DEFAULT;
    }
    if (Rounds > AIC_SCAN_ROUNDS_MAX) {
        Rounds = AIC_SCAN_ROUNDS_MAX;
    }
    if (Mode == AIC_SCAN_MODE_DRY) {
        return ScanDry();
    }
    Host = SdhciHostAt(1);
    if (Host == 0 || !SdhciWifiReady()) {
        SdhciMark("WiFi: scan needs a probed card (run wifiprobe first)\n");
        return -1;
    }
    if (AicWifiVendorPrepare(Host) != 0) {
        SdhciMark("WiFi: scan: vendor prepare failed\n");
        return -1;
    }
    if (Verbose != AIC_SCAN_VERBOSE_OFF) {
        SdhciMark("WiFi: scan verbose: dump after scan (best frame per AP)\n");
    }
    return ScanRun(Host, Verbose, Rounds);
}
