/*
 * WifiScan.c — `wififw scan [repeat N]`：ADD_IF 一次 → N 轮 SCANU_START + 收集 → 合并排版
 *
 * 用户眼里：一条命令把 STA 接口加上、广播扫描 2.4G 13 个信道 N 轮（默认 3），
 *           按 BSSID 合并后每个 AP 打一行，最后一行 summary 说明轮数/条数。
 *           `scan verbose` 再给每条合并结果附上“最强那帧”的原始字节取证。
 * 从哪读：SdhciWifiScan（板级入口）/ ScanDry（只拼包）/ ScanRun（多轮编排）。
 * 别改：ADD_IF 拿到的 vif_idx 必须原样带进每一轮 SCANU_START；VIF 只在最外层建一次，
 *       轮间不重加、不清合并表。结果用整帧接收（AicReceiveFrameTimeout），
 *       不能走定长拷参的 AicRecvMessageTimeout。
 *       本文件不发 ME_CONFIG/MM_START，也不碰 patch/下载状态。
 * 判据：0x1001 是“扫描结束”（rwnx_msg_rx.c:586 → cfg80211_scan_done），先到先收尾
 *       （排空一轮）；真机已验证固件不发 0x1001，所以单轮静默降到 2.5 s，
 *       单轮硬上限 6 s（远小于上一刀的 12 s）；结果统一在全部轮次后排版。
 */
#include "WifiScanPrivate.h"

/* 两条请求最长 376 B；UINT32 数组保证 4 B 对齐（结构体里有 UINT32/UINT16） */
static UINT32 sAicScanBuffer[(AIC_SCANU_START_LENGTH + 3u) / 4u];
/* 整帧落点：变长 payload 从这里直接解析，1600 B = 单条消息硬上限 AIC_MSG_MAX */
static UINT32 sAicScanFrame[(AIC_SCAN_FRAME_MAX + 3u) / 4u];

/* VIF 只建一次：ADD_IF 建的是持久对象，每扫一次再 add 会耗尽 VIF 表（真机第二次
 * scan 拿到 vif=1）。第一次的 vif_idx 缓存复用；若固件被 mmreset/重下载过，start
 * 会失败，ScanRun 丢掉缓存重加一次。 */
static UINT8 sAicScanVifIndex;
static int sAicScanVifValid;

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

/* 有缓存就复用（不再发 ADD_IF）；没有才真加。真机判据：第二次 scan 打 "add if reuse (vif=N)" */
static int ScanVifAcquire(SDHCI_HOST *Host, UINT8 *OutVifIndex) {
    if (sAicScanVifValid != 0) {
        *OutVifIndex = sAicScanVifIndex;
        SdhciMark("WiFi: add if reuse (vif=");
        SdhciMarkDecimal("", (UINT32)sAicScanVifIndex);
        SdhciMark(")\n");
        return 0;
    }
    if (ScanAddIf(Host, OutVifIndex) != 0) {
        return -1;
    }
    sAicScanVifIndex = *OutVifIndex;
    sAicScanVifValid = 1;
    return 0;
}

/*
 * PR-32：join 与扫描必须用**同一个 vif**（固件按 vif 记 BSS / 信道上下文）。
 * Cached 只读缓存、不碰卡 —— dry 用；Ensure 没有缓存就补一条 MM_ADD_IF —— 真发用。
 */
int AicScanVifCached(UINT8 *OutVifIndex) {
    if (sAicScanVifValid == 0) {
        return -1;
    }
    *OutVifIndex = sAicScanVifIndex;
    return 0;
}

int AicScanVifEnsure(SDHCI_HOST *Host, UINT8 *OutVifIndex) {
    if (sAicScanVifValid == 0) {
        SdhciMark("WiFi: join: no scan vif yet, adding one (MM_ADD_IF)\n");
    }
    return ScanVifAcquire(Host, OutVifIndex);
}

/*
 * mmreset 之后调用：固件的 vif 表已被复位清空，本模块缓存里的 vif 不再存在。
 * 只丢缓存，不改 scan/join 流程 —— 下一次 scan/join 的 ScanVifAcquire 会重新
 * 发 MM_ADD_IF（串口上从 "add if reuse" 变回 "add if"）。
 */
void AicScanVifForget(void) {
    sAicScanVifValid = 0;
    sAicScanVifIndex = 0;
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
 * 收一轮结果：0x1004 只解析 + 合并，不打印（打印留给 WifiScanReport.c 全部轮次后做）。
 * 0x1001 是固件的“扫描结束”（rwnx_msg_rx.c:586 → cfg80211_scan_done）：收到后
 * 继续排空队列，第一轮空（500 ms）就收尾，保证同批已到的结果先被读走。
 * 在 start 阶段就到（AckId）同样算已结束；没来才用 AIC_SCAN_IDLE_LIMIT 轮静默兜底。
 * 预算按轮算：AIC_SCAN_ROUND_BUDGET_MS / 轮，轮与轮之间不清 Report 的合并表。
 */
static int ScanCollect(SDHCI_HOST *Host, UINT16 AckId, int Verbose, UINT32 Round, UINT32 Rounds) {
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

        if (AicReceiveFrameTimeout(Host, (UINT8 *)sAicScanFrame, AIC_SCAN_FRAME_MAX, &Id,
                                    &FrameLength, &ParamLength, 1u, AIC_SCAN_RX_POLL_MS) != 0) {
            if (Done != 0) {
                End = AIC_SCAN_END_DONE; /* 0x1001 之后第一轮空：队列已排空 */
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
            AicScanReportAdd((const UINT8 *)sAicScanFrame, FrameLength, ParamLength);
        } else if (Id == (UINT16)AIC_SCANU_START_CFM) {
            Done = 1;
        } else {
            /* survey(0x004f) 等旁路消息仍即时打一行，量小且是信道覆盖的判据 */
            AicScanReportIgnored(Id, ParamLength, (const UINT8 *)sAicScanFrame, Verbose);
        }
    }
    if (Done != 0) {
        End = AIC_SCAN_END_DONE; /* 排空途中预算到期也算 done */
    }
    AicScanReportRoundDone((UINT32)(ScanNowMilliseconds() - Started), Done, End);
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

/* 一轮 = 同一条 vif 上的一次 SCANU_START + 收帧；轮间不加 vif、不清合并表 */
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
    int Reused = sAicScanVifValid;

    if (AicScanLayoutCheck() != 0) {
        return -1;
    }
    SdhciMark("WiFi: scan rounds=");
    SdhciMarkDecimal("", Rounds);
    SdhciMark(" (BSSID merge, add_if once, no cache clear between rounds)\n");
    AicScanReportReset(Rounds, Verbose);
    if (ScanVifAcquire(Host, &VifIndex) != 0) {
        SdhciMark("WiFi: scan FAILED at step 1/3 (add_if)\n");
        return -1;
    }
    for (Round = 1; Round <= Rounds; Round++) {
        AicScanReportRound(Round);
        if (ScanOneRound(Host, VifIndex, Verbose, Round, Rounds) == 0) {
            continue;
        }
        if (Round != 1u) {
            /* 后几轮 start 失败：保留已合并的轮次，仍然打 summary */
            SdhciMark("WiFi: scan: round ");
            SdhciMarkDecimal("", Round);
            SdhciMark(" scanu_start failed, keeping previous rounds\n");
            break;
        }
        if (Reused == 0) {
            SdhciMark("WiFi: scan FAILED at step 2/3 (scanu_start, round 1)\n");
            return -1;
        }
        /* 只有第一轮且走复用路径才自愈：mmreset/重下载固件后旧 vif 可能已失效 */
        SdhciMark("WiFi: scan: cached vif rejected, re-add once\n");
        sAicScanVifValid = 0;
        if (ScanVifAcquire(Host, &VifIndex) != 0) {
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
