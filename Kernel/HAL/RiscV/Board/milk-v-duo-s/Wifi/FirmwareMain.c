/*
 * WifiFw.c — Duo S AIC8800D80 固件下载（PR-30 S5b）入口与全局状态
 *
 * 本文件只留：全局状态 + AicFwFail + SdhciWifiFw 分发。
 * 检查 / 真写 / rd·wr 诊断拆到：
 *   WifiFirmwareCheck.c / WifiFirmwareWrite.c / WifiFirmwareDiagnose.c
 */
#include "Wifi/Private.h"

UINT8 gAicMessage[AIC_MSG_MAX];
UINT8 gAicRx[512];
UINT8 gAicFwBuffer[AIC_FW_CHUNK];

/*
 * 落点：adid/patch 两个地址**运行期从 patch 表里取**（见 AicPatchTableParse），
 * 这里的 0x0020B43C 只是"表里没有该字段"时的兜底，用兜底会打 `(fallback, ...)`。
 */
/*
 * 评审 P0-1：patch 表落地不再是独立命令，而是 write confirm 的第 4 个阶段。
 * 顺序：adid → patch → fmacfw → patchtbl(100 写 + cfg) → start_app。
 */
AIC_FW_STAGE gAicStages[AIC_FW_STAGE_MAX] = {
    {"adid", AIC_FW_ADID, AIC_D80_U02_ADID_ADDR, 1708u, 1, AIC_STAGE_FILE},
    {"patch", AIC_FW_PATCH, AIC_D80_U02_PATCH_ADDR, 30040u, 1, AIC_STAGE_FILE},
    {"fmacfw", AIC_FW_FMAC, AIC_RAM_FMAC_FW_ADDR, 331348u, 1, AIC_STAGE_FILE},
    {"patchtbl", 0, 0u, 0u, 1, AIC_STAGE_PATCHTBL},
    {"start", 0, AIC_RAM_FMAC_FW_ADDR, 0u, 1, AIC_STAGE_START},
};

/* 失败态：0=未跑 1=全部成功 2=失败（失败时 gWifiFwStageIndex 指向停在哪一阶段） */
UINT32 gWifiFwState;
/*
 * patch 管线是否已实现并应用。0 = 未开闸，confirm 一律拒绝；1 = 已开闸。
 * 2026-10-30 开闸：真机 dry 三条判据全过（见提交 e21645c6）。
 * volatile：故意让编译器不能把它折成常量。
 */
volatile UINT32 gAicPatchReady = 1u;
UINT32 gAicPatchState;
UINT32 gWifiFwStageIndex;
UINT32 gWifiFwSent;

AIC_PLAN_ITEM gAicPlan[AIC_PLAN_MAX];
/* MODE 那 9 个 pair 的最终值（pair 下标 0..8），自检用 */
UINT32 gAicModeValue[9];

void AicFwFail(const char *Stage, UINT32 Sent, const char *What) {
    gWifiFwState = 2u;
    SdhciMark("WiFi: stage=");
    SdhciMark(Stage);
    SdhciMark(" FAIL ");
    SdhciMark(What);
    SdhciMarkHex(" sent=", Sent, 8);
    SdhciMark("\n");
}

int SdhciWifiFw(UINT32 Mode) {
    SDHCI_HOST *Host = SdhciHostAt(1);
    int WantWrite = (Mode & (SDHCI_WIFI_FW_WRITE | SDHCI_WIFI_FW_CONFIRM)) != 0u;

    SdhciMark("WiFi: fw stage start\n");

    if ((Mode & SDHCI_WIFI_FW_STAT) != 0u) {
        if (WifiFirmwareStatCheck() != 0) {
            return -1;
        }
    }

    if ((Mode & SDHCI_WIFI_FW_ID) != 0u) {
        if (WifiFirmwareChipRevision() != 0) {
            return -1;
        }
    }

    if ((Mode & SDHCI_WIFI_FW_PATCH_DRY) != 0u) {
        if (AicPatchDry() != 0) {
            SdhciMark("WiFi: patch dry end FAILED\n");
            return -1;
        }
        SdhciMark("WiFi: patch dry end ok\n");
        return 0;
    }

    if (WantWrite != 0) {
        if (Host == 0 || !SdhciWifiReady()) {
            SdhciMark("WiFi: fw needs a probed card (run wifiprobe first)\n");
            return -1;
        }
        if (!SdhciWifiChipIsD80()) {
            SdhciMark("WiFi: chip is not aic8800d80, refuse to write firmware\n");
            return -1;
        }
        if ((Mode & SDHCI_WIFI_FW_CONFIRM) == 0u) {
            if (WifiFirmwareCheck(Host) != 0) {
                return -1;
            }
            SdhciMark("WiFi: fw stage end (checked only)\n");
            return 0;
        }
        if (gAicPatchReady == 0u) {
            SdhciMark("WiFi: write refused: patch stage not armed for this build\n");
            SdhciMark("WiFi: need: successful 'wififw patch dry' + user approval (gAicPatchReady)\n");
            SdhciMark("WiFi: see Archive/Duo-WiFi-patch-B2 review (P0-1..P0-3)\n");
            SdhciMark("WiFi: nothing written, nothing started\n");
            gWifiFwState = 2u;
            SdhciMark("WiFi: fw stage end (refused)\n");
            return -1;
        }
        if (WifiFirmwareWrite(Host) != 0) {
            SdhciMarkDecimal("WiFi: fw failed at stage ", gWifiFwStageIndex + 1u);
            SdhciMark(", sent=");
            SdhciMarkDecimal("", gWifiFwSent);
            SdhciMark(" of current stage\n");
            SdhciMark("WiFi: fw stage end (failed)\n");
            return -1;
        }
    }

    SdhciMark("WiFi: fw stage end\n");
    return 0;
}
