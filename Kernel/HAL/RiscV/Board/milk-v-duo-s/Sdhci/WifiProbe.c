/*
 * WifiProbe.c — 固件探针：MM_VERSION_REQ 判活、MM_RESET_REQ 复位后复查
 *
 * 从 WifiFw.c 原样搬出（v1 纯搬家，函数体逐字节不动）。
 * v2：mmreset 成功后清掉本模块自己的 vif/扫描/join 缓存（见文末注释）。
 * 判据：DBG 任务 ROM 里就有，MM 任务只有下载进去的固件才实现。
 */
#include "WifiPrivate.h"
#include "WifiScanPrivate.h"
#include "WifiAssocPrivate.h"

/*
 * `wififw ver`：发 MM_VERSION_REQ（id=0x0004, dest=TASK_MM=0, plen=0），等 MM_VERSION_CFM(0x0005)。
 *
 * 为什么它是"固件到底跑没跑"最干净的判据：
 *   DBG 任务（mem read/write、start_app）**ROM 里就有**，所以 ROM 阶段也能应答；
 *   MM 任务只有**下载进去的固件**才实现 —— 收到 version cfm 就证明 fmacfw 已经在跑。
 *
 * 只读：不发写消息、不改任何内存、不动 patch 状态。
 */
/*
 * 只读：读固件的 efuse MAC（厂商 rwnx_main.c:7072 在 MM_RESET 之前就取它，
 * 并把它用作 MM_ADD_IF 的 MAC）。我们 add_if 用的是硬编码 02:00:00:00:00:01；
 * 若固件单播过滤按 efuse MAC 做，单播帧（含 EAPOL）会被整片丢掉、广播/管理帧照过。
 * 这条查询就是用来判死/判活该假设的：一行日志，不需要额外动作。
 */
static void WifiFwMacQuery(SDHCI_HOST *Host) {
    UINT8 Cfm[32];
    UINT32 CfmLength = 0;

    if (AicSendMessageEx(Host, (UINT16)AIC_MM_GET_MAC_ADDR_REQ, (UINT16)AIC_TASK_MM, 0, 0u) != 0) {
        SdhciMark("WiFi: mac: msg send FAILED\n");
        return;
    }
    if (AicRecvMessageTimeout(Host, (UINT16)AIC_MM_GET_MAC_ADDR_CFM, Cfm, sizeof(Cfm), &CfmLength,
                              4u, AIC_CFM_TIMEOUT_MS) != 0) {
        SdhciMark("WiFi: mac: no cfm (fw may not answer this id)\n");
        return;
    }
    if (CfmLength < AIC_MM_GET_MAC_ADDR_LENGTH) {
        SdhciMarkHex("WiFi: mac: cfm too short plen=", CfmLength, 4);
        SdhciMark("\n");
        return;
    }
    SdhciMark("WiFi: fw mac=");
    SdhciMarkHex("", Cfm[0], 2);
    SdhciMarkHex(":", (UINT32)Cfm[1], 2);
    SdhciMarkHex(":", (UINT32)Cfm[2], 2);
    SdhciMarkHex(":", (UINT32)Cfm[3], 2);
    SdhciMarkHex(":", (UINT32)Cfm[4], 2);
    SdhciMarkHex(":", (UINT32)Cfm[5], 2);
    SdhciMark(" (add_if uses 02:00:00:00:00:01)\n");
}

static int WifiFwVersionProbe(SDHCI_HOST *Host) {
    UINT8 Cfm[64];
    UINT32 CfmLength = 0;
    UINT32 k;

    SdhciMarkHex("WiFi: msg tx id=", AIC_MM_VERSION_REQ, 4);
    SdhciMarkHex(" dest=", AIC_TASK_MM, 4);
    SdhciMarkHex(" src=", AIC_DRV_TASK_ID, 4);
    SdhciMark(" plen=0\n");
    if (AicSendMessageEx(Host, (UINT16)AIC_MM_VERSION_REQ, (UINT16)AIC_TASK_MM, 0, 0u) != 0) {
        SdhciMark("WiFi: ver: msg send FAILED\n");
        return -1;
    }
    /* 2 s 预算（4 × 500 ms），期间轮询 fn1 pending */
    if (AicRecvMessageTimeout(Host, (UINT16)AIC_MM_VERSION_CFM, Cfm, sizeof(Cfm), &CfmLength, 4u,
                          AIC_CFM_TIMEOUT_MS) != 0) {
        SdhciMark("WiFi: ver: no cfm in 2000ms  (firmware may not be running)\n");
        return -1;
    }
    SdhciMarkHex("WiFi: rx cfm id=", AIC_MM_VERSION_CFM, 4);
    SdhciMarkHex(" plen=", CfmLength, 4);
    SdhciMark("\n");
    AicDumpBytes(Cfm, CfmLength);
    /* struct mm_version_cfm 前 4 个 u32（lmac_msg.h:711-730） */
    SdhciMark("WiFi: ver");
    for (k = 0; k + 4u <= CfmLength && k < 20u; k += 4u) {
        UINT32 V = (UINT32)Cfm[k] | ((UINT32)Cfm[k + 1] << 8) | ((UINT32)Cfm[k + 2] << 16) |
                   ((UINT32)Cfm[k + 3] << 24);
        if (k == 0u) {
            SdhciMarkHex(" lmac=", V, 8);
        } else if (k == 4u) {
            SdhciMarkHex(" machw1=", V, 8);
        } else if (k == 8u) {
            SdhciMarkHex(" machw2=", V, 8);
        } else if (k == 12u) {
            SdhciMarkHex(" phy1=", V, 8);
        } else if (k == 16u) {
            SdhciMarkHex(" phy2=", V, 8);
        }
    }
    SdhciMark("\n");
    if (CfmLength < 16u) {
        SdhciMarkHex("WiFi: ver: cfm plen<16, only ", CfmLength, 4);
        SdhciMark(" bytes available\n");
    }
    /* 顺手读 efuse MAC（只读诊断，见 WifiFwMacQuery 的注释） */
    WifiFwMacQuery(Host);
    return 0;
}

int SdhciWifiFwVersion(void) {
    SDHCI_HOST *Host = SdhciHostAt(1);

    if (Host == 0 || !SdhciWifiReady()) {
        SdhciMark("WiFi: ver needs a probed card (run wifiprobe first)\n");
        return -1;
    }
    if (AicWifiVendorPrepare(Host) != 0) {
        SdhciMark("WiFi: ver: vendor prepare failed\n");
        return -1;
    }
    return WifiFwVersionProbe(Host);
}

/*
 * `wififw mmreset`：MM_RESET_REQ(0x0000) → MM_RESET_CFM(0x0001)，复位后自动再查一次版本。
 * 复位后固件应当仍在跑（MM 任务属于固件），版本不变即可确认"复位没把它打死"。
 */
int SdhciWifiFwMmReset(void) {
    SDHCI_HOST *Host = SdhciHostAt(1);
    UINT8 Cfm[16];
    UINT32 CfmLength = 0;

    if (Host == 0 || !SdhciWifiReady()) {
        SdhciMark("WiFi: mmreset needs a probed card (run wifiprobe first)\n");
        return -1;
    }
    if (AicWifiVendorPrepare(Host) != 0) {
        SdhciMark("WiFi: mmreset: vendor prepare failed\n");
        return -1;
    }
    SdhciMarkHex("WiFi: msg tx id=", AIC_MM_RESET_REQ, 4);
    SdhciMarkHex(" dest=", AIC_TASK_MM, 4);
    SdhciMarkHex(" src=", AIC_DRV_TASK_ID, 4);
    SdhciMark(" plen=0\n");
    if (AicSendMessageEx(Host, (UINT16)AIC_MM_RESET_REQ, (UINT16)AIC_TASK_MM, 0, 0u) != 0) {
        SdhciMark("WiFi: mm reset: msg send FAILED\n");
        return -1;
    }
    if (AicRecvMessageTimeout(Host, (UINT16)AIC_MM_RESET_CFM, Cfm, sizeof(Cfm), &CfmLength, 4u,
                          AIC_CFM_TIMEOUT_MS) != 0) {
        SdhciMark("WiFi: mm reset: no cfm in 2000ms\n");
        return -1;
    }
    SdhciMarkHex("WiFi: rx cfm id=", AIC_MM_RESET_CFM, 4);
    SdhciMarkHex(" plen=", CfmLength, 4);
    SdhciMark("\n");
    SdhciMark("WiFi: mm reset ok\n");
    /*
     * 真机：mmreset → bringup → scan → join 曾必然 SM_CONNECT_CFM status=0x04。
     * MM 复位把固件侧的 vif / BSS / channel context 清空了，而本模块还缓存着
     * "scan 时建的 vif"、"上次关联的 ap_idx"、"上次扫描的合并表"；下一次 join
     * 走 "add if reuse" 拿旧 vif，固件按参数拒绝（0x04）。
     * 这里只丢缓存，不改 scan/join 流程：下一次 scan/join 重新 MM_ADD_IF，
     * 串口上应看到 "add if" 而不是 "add if reuse"。
     * CFM 一到就清（版本探针失败也不影响"固件已复位"这个事实）。
     */
    AicScanVifForget();
    AicScanReportClear();
    AicJoinIndexesClear();
    SdhciMark("WiFi: mm reset: dropped vif/scan/join caches"
              " (next scan/join re-adds the interface)\n");
    /* 复位后必须再验一次：CFM 只证明消息通路，版本才证明固件还活着 */
    if (WifiFwVersionProbe(Host) != 0) {
        SdhciMark("WiFi: mm reset: post-reset ver FAILED\n");
        return -1;
    }
    return 0;
}
