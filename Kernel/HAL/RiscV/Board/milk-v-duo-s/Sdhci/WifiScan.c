/*
 * WifiScan.c — `wififw scan`：ADD_IF → SCANU_START → 边收边打 SCANU_RESULT_IND
 *
 * 用户眼里：一条命令把 STA 接口加上、广播扫描 2.4G 13 个信道，然后逐条打印 AP。
 *           `scan verbose` 再给每条结果附上原始字节取证（WifiScanDump.c）。
 * 从哪读：SdhciWifiScan（板级入口）/ ScanDry（只拼包）/ ScanRun（三步编排）。
 * 别改：ADD_IF 拿到的 vif_idx 必须原样带进 SCANU_START；结果用整帧接收
 *       （AicReceiveFrameTimeout），不能走定长拷参的 AicRecvMessageTimeout。
 *       本文件不发 ME_CONFIG/MM_START，也不碰 patch/下载状态。
 * 预算：总 8 s、每轮 500 ms、连续 4 轮无结果就收工，别死等。
 */
#include "WifiScanPrivate.h"

/* 两条请求最长 376 B；UINT32 数组保证 4 B 对齐（结构体里有 UINT32/UINT16） */
static UINT32 sAicScanBuffer[(AIC_SCANU_START_LENGTH + 3u) / 4u];
/* 整帧落点：变长 payload 从这里直接解析，1600 B = 单条消息硬上限 AIC_MSG_MAX */
static UINT32 sAicScanFrame[(AIC_SCAN_FRAME_MAX + 3u) / 4u];

/* 毫秒计时：SdhciReadTime() 是 BOARD_TIMEBASE_HZ（25 MHz）的裸计数 */
static UINT32 ScanNowMilliseconds(void) {
    return (UINT32)(SdhciReadTime() / (BOARD_TIMEBASE_HZ / 1000u));
}

static void ScanPrintTx(const char *Name, UINT32 Length, const char *Note) {
    SdhciMark("WiFi: ");
    SdhciMark(Name);
    SdhciMark(" tx len=");
    SdhciMarkDecimal("", Length);
    SdhciMark(Note);
    SdhciMark("\n");
}

/* dry 用：前 64 B 足够看出结构体排歪没有 */
static void ScanDump(const UINT8 *Data, UINT32 Length) {
    UINT32 Count = Length > AIC_BRINGUP_DUMP_BYTES ? AIC_BRINGUP_DUMP_BYTES : Length;
    UINT32 Index;

    SdhciMark("WiFi:  dump[");
    SdhciMarkDecimal("", Count);
    SdhciMark("] of ");
    SdhciMarkDecimal("", Length);
    for (Index = 0; Index < Count; Index++) {
        SdhciMarkHex(" ", (UINT32)Data[Index], 2);
    }
    SdhciMark("\n");
}

/* 等 WantId（或 AlternateId，用于 0x1009 那条厂商实际在等的 CFM） */
static int ScanWaitId(SDHCI_HOST *Host, UINT16 WantId, UINT16 AlternateId, UINT16 *OutId,
                      UINT32 *OutParamLength) {
    UINT32 Try;

    for (Try = 0; Try < AIC_SCAN_CFM_TRIES; Try++) {
        UINT16 Id = 0;
        UINT32 ParamLength = 0;

        if (AicReceiveFrameTimeout(Host, (UINT8 *)sAicScanFrame, AIC_SCAN_FRAME_MAX, &Id, 0,
                                    &ParamLength, 1u, AIC_CFM_TIMEOUT_MS) != 0) {
            continue;
        }
        if (Id == WantId || Id == AlternateId) {
            *OutId = Id;
            *OutParamLength = ParamLength;
            return 0;
        }
        SdhciMarkHex("WiFi: scan: ignore rx id=", Id, 4);
        SdhciMarkHex(" plen=", ParamLength, 4);
        SdhciMark("\n");
    }
    return -1;
}

static int ScanAddIf(SDHCI_HOST *Host, UINT8 *OutVifIndex) {
    UINT32 Length = AicMmAddIfBuild((UINT8 *)sAicScanBuffer);
    UINT32 ParamLength = 0;
    UINT16 Id = 0;
    const AIC_ADD_IF_CFM *Confirm;

    ScanPrintTx("add if", Length, "");
    if (AicSendMessageEx(Host, (UINT16)AIC_MM_ADD_IF_REQ, (UINT16)AIC_TASK_MM, sAicScanBuffer,
                         (UINT16)Length) != 0) {
        SdhciMark("WiFi: add if send FAILED (SDIO tx)\n");
        return -1;
    }
    if (ScanWaitId(Host, (UINT16)AIC_MM_ADD_IF_CFM, (UINT16)AIC_SCAN_NO_ALTERNATE, &Id,
                   &ParamLength) != 0) {
        SdhciMark("WiFi: add if: no MM_ADD_IF_CFM(0x0007) in 2000ms\n");
        return -1;
    }
    if (ParamLength < AIC_ADD_IF_CFM_LENGTH) {
        SdhciMarkHex("WiFi: add if cfm too short plen=", ParamLength, 4);
        SdhciMark("\n");
        return -1;
    }
    Confirm = (const AIC_ADD_IF_CFM *)((const UINT8 *)sAicScanFrame + AIC_E2A_PARAM_OFF);
    if (Confirm->Status != 0u) {
        SdhciMarkHex("WiFi: add if cfm status=", Confirm->Status, 2);
        SdhciMark(" (firmware refused: no free VIF / bad MAC)\n");
        return -1;
    }
    *OutVifIndex = Confirm->InstanceNumber;
    SdhciMark("WiFi: add if cfm ok (vif=");
    SdhciMarkDecimal("", Confirm->InstanceNumber);
    SdhciMark(")\n");
    return 0;
}

static int ScanStart(SDHCI_HOST *Host, UINT8 VifIndex, UINT16 *OutAckId) {
    UINT32 Length = AicScanuStartBuild((UINT8 *)sAicScanBuffer, VifIndex);
    UINT32 ParamLength = 0;
    UINT16 Id = 0;

    ScanPrintTx("scanu start", Length, " (2.4G 13 ch, ssid_cnt=0)");
    if (AicSendMessageEx(Host, (UINT16)AIC_SCANU_START_REQ, (UINT16)AIC_TASK_SCANU,
                         sAicScanBuffer, (UINT16)Length) != 0) {
        SdhciMark("WiFi: scanu start send FAILED (SDIO tx)\n");
        return -1;
    }
    if (ScanWaitId(Host, (UINT16)AIC_SCANU_START_CFM, (UINT16)AIC_SCANU_START_CFM_ADDITIONAL,
                   &Id, &ParamLength) != 0) {
        SdhciMark("WiFi: scanu start: no 0x1001/0x1009 in 2000ms\n");
        return -1;
    }
    /* 厂商实际等的是 0x1009；0x1001 的 3 B 参数里 status 在 +1 */
    if (Id == AIC_SCANU_START_CFM) {
        const AIC_SCANU_START_CONFIRM *Confirm =
            (const AIC_SCANU_START_CONFIRM *)((const UINT8 *)sAicScanFrame + AIC_E2A_PARAM_OFF);

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

/*
 * 收结果：SCANU_RESULT_IND(0x1004) 边收边打；SCANU_START_CFM(0x1001) 是扫描结束。
 * AckId 是 start 阶段收到的 CFM：若它本身就是 0x1001，0 结果也按“扫完了”算。
 */
static int ScanCollect(SDHCI_HOST *Host, UINT16 AckId, int Verbose) {
    UINT32 Started = ScanNowMilliseconds();
    UINT32 Idle = 0;
    UINT32 Count = 0;

    SdhciMark("WiFi: scan collecting (budget 8000ms, idle 2000ms)");
    SdhciMark(Verbose != AIC_SCAN_VERBOSE_OFF ? " verbose\n" : "\n");
    while ((UINT32)(ScanNowMilliseconds() - Started) < AIC_SCAN_TOTAL_BUDGET_MS) {
        UINT16 Id = 0;
        UINT32 FrameLength = 0;
        UINT32 ParamLength = 0;

        if (AicReceiveFrameTimeout(Host, (UINT8 *)sAicScanFrame, AIC_SCAN_FRAME_MAX, &Id,
                                    &FrameLength, &ParamLength, 1u, AIC_SCAN_RX_POLL_MS) != 0) {
            Idle++;
            if (Idle >= AIC_SCAN_IDLE_LIMIT) {
                break;
            }
            continue;
        }
        if (Id == AIC_SCANU_RESULT_IND) {
            Idle = 0;
            Count++;
            AicScanResultPrint((const UINT8 *)sAicScanFrame, FrameLength, ParamLength, Count,
                               Verbose);
        } else if (Id == AIC_SCANU_START_CFM) {
            SdhciMark("WiFi: scan done ind (0x1001)\n");
            SdhciMark("WiFi: scan done, ");
            SdhciMarkDecimal("", Count);
            SdhciMark(" results\n");
            return 0;
        } else {
            AicScanReportIgnored(Id, ParamLength, (const UINT8 *)sAicScanFrame, Verbose);
        }
    }
    if (Count == 0u) {
        if (AckId == AIC_SCANU_START_CFM) {
            SdhciMark("WiFi: scan done, 0 results (0x1001 was the only cfm)\n");
            return 0;
        }
        SdhciMark("WiFi: scan result FAILED: no SCANU_RESULT_IND (0x1004) in budget\n");
        SdhciMark("WiFi:   start cfm was ok, so firmware took the scan but sent no BSS\n");
        return -1;
    }
    SdhciMark("WiFi: scan done, ");
    SdhciMarkDecimal("", Count);
    SdhciMark(" results (budget/idle reached, no 0x1001)\n");
    return 0;
}

static int ScanDry(void) {
    UINT32 Length;

    if (AicScanLayoutCheck() != 0) {
        return -1;
    }
    SdhciMark("WiFi: scan dry: build only, nothing sent\n");
    Length = AicMmAddIfBuild((UINT8 *)sAicScanBuffer);
    ScanPrintTx("add if", Length, "");
    ScanDump((const UINT8 *)sAicScanBuffer, Length);
    Length = AicScanuStartBuild((UINT8 *)sAicScanBuffer, (UINT8)AIC_SCAN_DRY_VIF_INDEX);
    ScanPrintTx("scanu start", Length, " (2.4G 13 ch, ssid_cnt=0)");
    ScanDump((const UINT8 *)sAicScanBuffer, Length);
    SdhciMark("WiFi: scan dry ok (2 built, 0 sent)\n");
    return 0;
}

static int ScanRun(SDHCI_HOST *Host, int Verbose) {
    UINT8 VifIndex = 0;
    UINT16 AckId = 0;

    if (AicScanLayoutCheck() != 0) {
        return -1;
    }
    if (ScanAddIf(Host, &VifIndex) != 0) {
        SdhciMark("WiFi: scan FAILED at step 1/3 (add_if)\n");
        return -1;
    }
    if (ScanStart(Host, VifIndex, &AckId) != 0) {
        SdhciMark("WiFi: scan FAILED at step 2/3 (scanu_start)\n");
        return -1;
    }
    if (ScanCollect(Host, AckId, Verbose) != 0) {
        SdhciMark("WiFi: scan FAILED at step 3/3 (receive results)\n");
        return -1;
    }
    return 0;
}

int SdhciWifiScan(int Mode) {
    SDHCI_HOST *Host;
    int Verbose = Mode == AIC_SCAN_MODE_VERBOSE ? AIC_SCAN_VERBOSE_ON : AIC_SCAN_VERBOSE_OFF;

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
        SdhciMark("WiFi: scan verbose: raw bytes on (payload head 96 B, dot11 header, IE walk)\n");
    }
    return ScanRun(Host, Verbose);
}
