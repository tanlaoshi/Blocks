/*
 * WifiAssoc.c — `wififw join`：SM_CONNECT_REQ(0x1800) → 等 CFM(0x1801) → 等 IND(0x1802)
 *
 * 用户眼里：一条命令把"发起关联"这件事做完 —— 从扫描缓存里挑目标、用扫描时建的 vif
 *           发一条 SM_CONNECT_REQ，先看固件受不受理（CFM），再看空口成没成（IND）；
 *           给了密码就连着把控制端口与四次握手走完（JoinAfterAssoc）。
 * 从哪读：JoinRun（真发）/ JoinDry（只拼包）/ JoinAfterAssoc（关联之后那段）/ SdhciWifiJoin。
 * 别改：**关联是 LMAC 干的**，主机不发 auth/assoc 帧，只填一条消息（见调研文档 §1）。
 *       CFM 的 status 只代表"受理"，IND 的 status_code 才代表"关联结果"。本文件不写
 *       内存、不动 patch 状态、不碰 bringup/scan 的缓存。
 *
 * 本刀（PR-34）：IND 成功之后接 ME_SET_CONTROL_PORT_REQ(0x1404) 与四次握手；
 *       收/发 EAPOL 都不在本文件（收在 WifiEapol.c，发在 WifiEapolTx.c，顺序在
 *       WifiHandshake.c）。**装钥匙（MM_KEY_ADD_REQ 0x0024）仍不在本刀**，
 *       字段表见调研文档 §5，sta_idx 已由 AicJoinLastIndexes 留下。
 */
#include "WifiAssocPrivate.h"
#include "WifiHandshakePrivate.h"
#include "CString.h"

/* 320 B 参数 + 1600 B 整帧；UINT32 数组保证 4 B 对齐（镜像里有 UINT32 成员） */
static UINT32 sAicJoinBuffer[(AIC_SM_CONNECT_LENGTH + 3u) / 4u];
static UINT32 sAicJoinFrame[(AIC_JOIN_FRAME_MAX + 3u) / 4u];

/*
 * 等 WantId。每次 try 只等 500 ms（AIC_CFM_TIMEOUT_MS），所以 N 次 = 最多 N×500 ms；
 * 队列里插队的消息（扫描结果、survey 测量）只打一行就丢，不占额外等待。
 */
static int JoinWaitId(SDHCI_HOST *Host, UINT16 WantId, UINT32 Tries, UINT32 *OutParamLength) {
    UINT32 Try;

    for (Try = 0; Try < Tries; Try++) {
        UINT16 Id = 0;
        UINT32 ParamLength = 0;

        if (AicReceiveFrameTimeout(Host, (UINT8 *)sAicJoinFrame, AIC_JOIN_FRAME_MAX, &Id, 0,
                                   &ParamLength, 1u, AIC_CFM_TIMEOUT_MS) != 0) {
            continue;
        }
        if (Id == WantId) {
            *OutParamLength = ParamLength;
            return 0;
        }
        SdhciMarkHex("WiFi: join: ignore rx id=", Id, 4);
        SdhciMarkHex(" plen=", ParamLength, 4);
        SdhciMark("\n");
    }
    return -1;
}

static int JoinSend(SDHCI_HOST *Host, UINT32 Length) {
    AicJoinPrintTx(Length);
    if (AicSendMessageEx(Host, (UINT16)AIC_SM_CONNECT_REQ, (UINT16)AIC_TASK_SM,
                         sAicJoinBuffer, (UINT16)Length) != 0) {
        SdhciMark("WiFi: join FAILED at step 1/2: SM_CONNECT_REQ(0x1800) send failed (SDIO tx)\n");
        return -1;
    }
    return 0;
}

/* CFM 只有 1 B 的 status：非 0 = 固件当场拒绝（还没上空口），和 IND 的失败不是一回事 */
static int JoinWaitCfm(SDHCI_HOST *Host) {
    UINT32 ParamLength = 0;
    const AIC_SM_CONNECT_CONFIRM *Confirm;

    if (JoinWaitId(Host, (UINT16)AIC_SM_CONNECT_CFM, AIC_JOIN_CFM_TRIES, &ParamLength) != 0) {
        SdhciMark("WiFi: join FAILED at step 1/2: no SM_CONNECT_CFM(0x1801) in 2000ms");
        SdhciMark(" (firmware did not answer; bringup + scan first)\n");
        return -1;
    }
    if (ParamLength < AIC_SM_CONNECT_CFM_LENGTH) {
        SdhciMarkHex("WiFi: join FAILED at step 1/2: SM_CONNECT_CFM too short plen=", ParamLength,
                     4);
        SdhciMark("\n");
        return -1;
    }
    Confirm = (const AIC_SM_CONNECT_CONFIRM *)((const UINT8 *)sAicJoinFrame + AIC_E2A_PARAM_OFF);
    if (Confirm->Status != 0u) {
        SdhciMarkHex("WiFi: join FAILED at step 1/2: SM_CONNECT_CFM status=0x", Confirm->Status, 2);
        SdhciMark(" (firmware refused before auth: busy / bad vif / unsupported param)\n");
        return -1;
    }
    SdhciMark("WiFi: join cfm ok (status=0x00, auth procedure started)\n");
    return 0;
}

static int JoinWaitInd(SDHCI_HOST *Host) {
    UINT32 ParamLength = 0;

    const AIC_SM_CONNECT_INDICATION *Ind;

    if (JoinWaitId(Host, (UINT16)AIC_SM_CONNECT_IND, AIC_JOIN_IND_TRIES, &ParamLength) != 0) {
        SdhciMark("WiFi: join FAILED at step 2/2: no SM_CONNECT_IND(0x1802) in 3000ms");
        SdhciMark(" (CFM ok but auth/assoc got no answer)\n");
        return -1;
    }
    Ind = (const AIC_SM_CONNECT_INDICATION *)((const UINT8 *)sAicJoinFrame + AIC_E2A_PARAM_OFF);
    /*
     * 抢开控制端口（PR-35）：AP 的 EAPOL M1 在关联完成的一瞬间就到，而 AicJoinHandleInd
     * 要打三四行串口（115200 下每行 5~10 ms），等它打完再开端口，M1 已在端口关着时
     * 被固件丢掉——真机现象是"只收到 beacon，收不到 M1"。
     * 所以：IND 一到、把 ap_idx 取出来就立刻发 0x1404，**打印全部推迟到之后**。
     * 关联失败（status != 0）就不开，避免对不存在的 sta 发消息。
     */
    if (ParamLength >= (UINT32)sizeof(AIC_SM_CONNECT_INDICATION) && Ind->StatusCode == 0u) {
        (void)AicControlPortSend(Host, Ind->ApIndex);
    }
    return AicJoinHandleInd((const UINT8 *)sAicJoinFrame + AIC_E2A_PARAM_OFF, ParamLength);
}

/*
 * 真发：vif 必须与扫描时同一个（固件按 vif 记 BSS/channel context）。
 * AicScanVifEnsure 有缓存就复用，没有才补一条 MM_ADD_IF —— 所以"刚 bringup 完
 * 没扫过"也能 join（会先建 if，但目标仍然要求来自扫描缓存）。
 */
/*
 * 关联成功之后的四步（控制端口 → M1 → PTK/M2 → M3），全部在 WifiHandshake.c。
 * **本函数不插任何延时**：控制端口一开，AP 的 M1 就在固件 RX FIFO 里等我们读。
 * Password 为空 = 只到控制端口为止（`control port open` 那行照打）。
 */
static int JoinAfterAssoc(SDHCI_HOST *Host, const AIC_SCAN_AP *Target, const char *Password) {
    UINT8 VifIndex = 0;
    UINT8 ApIndex = 0;

    /* IND 已经打出来的两个索引：vif 用于打印，ap_idx 是 0x1404 的 sta_idx */
    AicJoinLastIndexes(&VifIndex, &ApIndex);
    return AicWifiHandshakeRun(Host, Target, Password, VifIndex, ApIndex);
}

/*
 * 停扫：SCANU_CANCEL_REQ(0x100A) → 等 CFM(0x100B)。无参数，CFM 1 字节 status。
 * 打一行就够：失败不阻塞 join（下一步照发 SM_CONNECT，让固件自己判）。
 */
static int JoinCancelScan(SDHCI_HOST *Host) {
    UINT8 Cfm[8];
    UINT32 CfmLength = 0;

    if (AicSendMessageEx(Host, (UINT16)AIC_SCANU_CANCEL_REQ, (UINT16)AIC_TASK_SCANU, 0, 0u) != 0) {
        return -1;
    }
    if (AicRecvMessageTimeout(Host, (UINT16)AIC_SCANU_CANCEL_CFM, Cfm, sizeof(Cfm), &CfmLength, 2u,
                              AIC_CFM_TIMEOUT_MS) != 0) {
        SdhciMark("WiFi: join: scan cancel cfm timeout (continue)\n");
        return -1;
    }
    SdhciMarkHex("WiFi: join: scan cancelled (status=", CfmLength > 0u ? (UINT32)Cfm[0] : 0xFFu, 2);
    SdhciMark(")\n");
    return 0;
}

static int JoinRun(SDHCI_HOST *Host, const AIC_SCAN_AP *Target, const char *Password) {
    UINT8 VifIndex = 0;
    UINT32 Length;

    if (AicJoinLayoutCheck() != 0) {
        return -1;
    }
    if (AicScanVifEnsure(Host, &VifIndex) != 0) {
        SdhciMark("WiFi: join FAILED at step 0/2: no vif (MM_ADD_IF failed)\n");
        return -1;
    }
    AicJoinPrintTarget(Target);
    SdhciMark("WiFi: join vif=");
    SdhciMarkDecimal("", VifIndex);
    SdhciMark("\n");
    /*
     * 先把可能残留的扫描停掉（厂商在 connect 前若还在扫会 abort，见 rwnx_main.c:3575）。
     * 我们连打多轮 scan 后直接 join，固件可能仍在扫：那样只会继续上送 beacon，
     * 数据帧（含 EAPOL M1）不上送 —— 真机现象正是如此。取消失败不阻塞 join。
     */
    (void)JoinCancelScan(Host);
    Length = AicJoinBuild((UINT8 *)sAicJoinBuffer, Target, VifIndex);
    if (JoinSend(Host, Length) != 0) {
        return -1;
    }
    if (JoinWaitCfm(Host) != 0) {
        return -1;
    }
    if (JoinWaitInd(Host) != 0) {
        return -1;
    }
    return JoinAfterAssoc(Host, Target, Password);
}

/*
 * dry：不需要卡、不需要固件。目标能查到就用真目标（这样 dry 打出来的包和真发一致）；
 * 查不到就降级成 bssid=全 FF / freq=0，但**明说**这是占位，别当成"真会这么发"。
 */
static int JoinDry(const char *Ssid, const char *BssidText) {
    AIC_SCAN_AP Target;
    UINT8 VifIndex = AIC_JOIN_VIF_INDEX_DRY;
    int Status;
    UINT32 Length;

    if (AicJoinLayoutCheck() != 0) {
        return -1;
    }
    SdhciMark("WiFi: join dry: build only, nothing sent\n");
    Status = AicJoinResolve(Ssid, BssidText, &Target);
    if (Status != 0) {
        AicJoinPlaceholder(Ssid, &Target);
        if (Status < 0) {
            SdhciMark("WiFi: join dry: bad bssid text, using bssid=ff:ff:ff:ff:ff:ff freq=0\n");
        } else if (AicScanReportCount() == 0u) {
            SdhciMark("WiFi: join dry: no scan cache (run 'wififw scan' first),"
                      " bssid=ff:ff:ff:ff:ff:ff freq=0\n");
        } else {
            SdhciMark("WiFi: join dry: ssid not in scan cache,"
                      " bssid=ff:ff:ff:ff:ff:ff freq=0\n");
        }
    }
    if (AicScanVifCached(&VifIndex) != 0) {
        VifIndex = AIC_JOIN_VIF_INDEX_DRY;
        SdhciMark("WiFi: join dry: no vif yet, vif=0 (real run would send MM_ADD_IF)\n");
    }
    AicJoinPrintTarget(&Target);
    SdhciMark("WiFi: join vif=");
    SdhciMarkDecimal("", VifIndex);
    SdhciMark("\n");
    Length = AicJoinBuild((UINT8 *)sAicJoinBuffer, &Target, VifIndex);
    AicJoinPrintTx(Length);
    AicJoinDump((const UINT8 *)sAicJoinBuffer, Length);
    /* ie_buf 在 +64，落在上面 64 B dump 之外，所以单独打；本刀唯一新填的就是它 */
    AicJoinDumpRsn((const UINT8 *)sAicJoinBuffer, Length);
    SdhciMark("WiFi: join dry ok (1 built, 0 sent)\n");
    return 0;
}

int SdhciWifiJoin(int Dry, const char *Ssid, const char *BssidText, const char *Password) {
    AIC_SCAN_AP Target;
    SDHCI_HOST *Host;
    int Status;

    if (Ssid == 0 || Ssid[0] == 0) {
        SdhciMark("WiFi: join: empty ssid\n");
        return -1;
    }
    if (strlen(Ssid) > AIC_JOIN_SSID_MAX) {
        SdhciMark("WiFi: join: ssid longer than 32 bytes\n");
        return -1;
    }
    if (Dry != 0) {
        return JoinDry(Ssid, BssidText);
    }
    Status = AicJoinResolve(Ssid, BssidText, &Target);
    if (Status < 0) {
        SdhciMark("WiFi: join: bad bssid (want aa:bb:cc:dd:ee:ff)\n");
        return -1;
    }
    if (Status > 0) {
        SdhciMark("WiFi: join: target not found in scan cache: ssid=\"");
        SdhciMark(Ssid);
        SdhciMark("\" (run 'wififw scan' first)\n");
        return -1;
    }
    Host = SdhciHostAt(1);
    if (Host == 0 || !SdhciWifiReady()) {
        SdhciMark("WiFi: join needs a probed card (run wifiprobe first)\n");
        return -1;
    }
    if (AicWifiVendorPrepare(Host) != 0) {
        SdhciMark("WiFi: join: vendor prepare failed\n");
        return -1;
    }
    return JoinRun(Host, &Target, Password);
}

/* 探针入口（给 shell 的 wififw gtk 用）：用已保存的 GTK 重装组密钥，不重新关联。 */
int SdhciWifiGtkReinstall(void) {
    SDHCI_HOST *Host = SdhciHostAt(1);
    UINT8 VifIndex = 0;

    if (Host == 0 || !SdhciWifiReady()) {
        return -1;
    }
    if (AicScanVifEnsure(Host, &VifIndex) != 0) {
        return -1;
    }
    return AicHandshakeReinstallGtk(Host, VifIndex);
}
