/*
 * WifiBringup.c — 让固件"能配置无线电"：ME_CONFIG → CHAN → MM_START → coex
 *                 →（D80）txpwr_lvl_v3；默认不发 MM_SET_RF_CALIB（Docs/20 §1e）
 *
 * 用户眼里：`wififw bringup` 一条命令把步骤发完，每步各自等 CFM，任一步失败就停。
 * 从哪读：BringupRun（三步循环）/ BringupDry（只拼包打印）/ SdhciWifiBringup（入口）。
 * 别改：ME 三步顺序 —— MM_START 之前必须有 ME_CONFIG + ME_CHAN_CONFIG。
 * D80 RF：厂商 probe 为 rf_init→RESET→ME；本仓真机 RF_CALIB 无 CFM（§1b–1d），
 *       且无 CFM 会卡 MM。§1e：默认只发 txpwr，**不发** RF_CALIB（一次一变量）。
 */
#include "Wifi/Private.h"

/* 三步的顺序、消息 id、人话名字与失败原因，全部集中在这张表里 */
typedef struct {
    UINT16 RequestId;      /* 发出去的 REQ */
    UINT16 ConfirmId;      /* 等回来的 CFM */
    UINT16 TaskId;         /* dest_id：TASK_MM=0 / TASK_ME=5 */
    const char *Name;      /* 日志里的短名，如 "me config" */
    const char *TxNote;    /* " tx len=N" 之后追加的注解，可为 "" */
    const char *Reason;    /* 超时时给的人话原因 */
    UINT32 (*Build)(UINT8 *Buffer); /* 拼参数，返回参数长度 */
} AIC_BRINGUP_STEP;

static const AIC_BRINGUP_STEP sAicBringupSteps[] = {
    {AIC_ME_CONFIG_REQ, AIC_ME_CONFIG_CFM, AIC_TASK_ME, "me config", "",
     "ME_CONFIG(0x1400) got no CFM in 2000ms: firmware may not know this struct version",
     AicMeConfigBuild},
    {AIC_ME_CHAN_CONFIG_REQ, AIC_ME_CHAN_CONFIG_CFM, AIC_TASK_ME, "chan config", " (2.4G 13 ch)",
     "ME_CHAN_CONFIG(0x1402) got no CFM in 2000ms: firmware has no channel table",
     AicChanConfigBuild},
    {AIC_MM_START_REQ, AIC_MM_START_CFM, AIC_TASK_MM, "mm start", "",
     "MM_START(0x0002) got no CFM in 2000ms: it needs the two ME messages accepted first",
     AicMmStartBuild},
    /* 厂商在 vif up（STA）时补发这条（rwnx_main.c:974-977）；我们此前没有，
       本轮验证"未配置共存"是否压住数据上送。失败只打一行，不影响 bringup 判据。 */
    {AIC_MM_SET_COEX_REQ, AIC_MM_SET_COEX_CFM, AIC_TASK_MM, "coex", " (sta, nullcts)",
     "MM_SET_COEX(0x0065) got no CFM in 2000ms: coex not configured (may affect rx upload)",
     AicCoexBuild},
};

#define AIC_BRINGUP_STEP_COUNT ((UINT32)(sizeof(sAicBringupSteps) / sizeof(sAicBringupSteps[0])))

/*
 * 参数缓冲：三条消息最长 254 B。用 UINT32 数组而不是 UINT8 数组，是为了保证
 * 4 B 对齐 —— 镜像结构体里有 UINT32 成员（mm_start.phy_cfg），不对齐会踩 RISC-V 的
 * 非对齐访问。WifiConfigMessage.c 的拼装函数就依赖这个前提。
 */
static UINT32 sAicBringupBuffer[(AIC_BRINGUP_MAX_LENGTH + 3u) / 4u];
/* CFM 这三种都没有参数（lmac_msg.h 里没有 me_config_cfm/me_chan_config_cfm/mm_start_cfm），
   缓冲只是给收包函数一个落点，宁可小也不能是空指针 */
static UINT8 sAicBringupConfirm[16];

static void BringupPrintTransmit(const AIC_BRINGUP_STEP *Step, UINT32 Length) {
    SdhciMark("WiFi: ");
    SdhciMark(Step->Name);
    SdhciMark(" tx len=");
    SdhciMarkDecimal("", Length);
    SdhciMark(Step->TxNote);
    SdhciMark("\n");
}

/* dry 用：前 64 B 足够看出结构体填错（头 64 B 覆盖 ht_cap 整段 + vht 整段） */
static void BringupDump(const UINT8 *Data, UINT32 Length) {
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

/* 发一条、等它的 CFM；成功打 "cfm ok"，失败打"人话原因"（调用方再打停在哪步） */
static int BringupSendStep(SDHCI_HOST *Host, const AIC_BRINGUP_STEP *Step, UINT32 Length) {
    UINT32 ConfirmLength = 0;

    BringupPrintTransmit(Step, Length);
    if (AicSendMessageEx(Host, Step->RequestId, Step->TaskId, (const void *)sAicBringupBuffer,
                        (UINT16)Length) != 0) {
        SdhciMark("WiFi: ");
        SdhciMark(Step->Name);
        SdhciMark(" send FAILED (SDIO tx)\n");
        return -1;
    }
    /* 4 × 500 ms = 2000 ms 预算，与 ver/mmreset 同一档 */
    if (AicRecvMessageTimeout(Host, Step->ConfirmId, sAicBringupConfirm,
                          (UINT32)sizeof(sAicBringupConfirm), &ConfirmLength,
                          AIC_BRINGUP_CFM_TRIES, AIC_CFM_TIMEOUT_MS) != 0) {
        SdhciMark("WiFi: ");
        SdhciMark(Step->Reason);
        SdhciMark("\n");
        return -1;
    }
    SdhciMark("WiFi: ");
    SdhciMark(Step->Name);
    SdhciMark(" cfm ok\n");
    return 0;
}

static int BringupDry(void) {
    UINT32 Index;
    UINT32 Length;

    if (AicBringupLayoutCheck() != 0) {
        return -1;
    }
    SdhciMark("WiFi: bringup dry: build only, nothing sent\n");
    for (Index = 0; Index < AIC_BRINGUP_STEP_COUNT; Index++) {
        const AIC_BRINGUP_STEP *Step = &sAicBringupSteps[Index];

        Length = Step->Build((UINT8 *)sAicBringupBuffer);
        BringupPrintTransmit(Step, Length);
        BringupDump((const UINT8 *)sAicBringupBuffer, Length);
    }
    SdhciMark("WiFi: bringup dry ok (");
    SdhciMarkDecimal("", AIC_BRINGUP_STEP_COUNT);
    SdhciMark(" built, 0 sent)\n");
    return 0;
}

static int BringupRun(SDHCI_HOST *Host) {
    UINT32 Index;
    UINT32 Length;

    if (AicBringupLayoutCheck() != 0) {
        return -1;
    }
    if (Host == 0 || !SdhciWifiReady()) {
        SdhciMark("WiFi: bringup needs a probed card (run wifiprobe first)\n");
        return -1;
    }
    if (AicWifiVendorPrepare(Host) != 0) {
        SdhciMark("WiFi: bringup: vendor prepare failed\n");
        return -1;
    }
    for (Index = 0; Index < AIC_BRINGUP_STEP_COUNT; Index++) {
        const AIC_BRINGUP_STEP *Step = &sAicBringupSteps[Index];

        Length = Step->Build((UINT8 *)sAicBringupBuffer);
        if (BringupSendStep(Host, Step, Length) != 0) {
            SdhciMark("WiFi: bringup FAILED at step ");
            SdhciMarkDecimal("", Index + 1u);
            SdhciMark("/");
            SdhciMarkDecimal("", AIC_BRINGUP_STEP_COUNT);
            SdhciMark(" (");
            SdhciMark(Step->Name);
            SdhciMark("), ok=");
            SdhciMarkDecimal("", Index);
            SdhciMark("\n");
            return -1;
        }
    }
    /*
     * D80 RF §1e：只发 txpwr（有 CFM）。默认不发 RF_CALIB——无 CFM 的 0x69
     * 曾卡 MM（§1c/1d）；本刀证伪「永不发 0x69 能否让单播通」。
     */
    if (AicWifiTransmitPowerLevelSend(Host) != 0) {
        SdhciMark("WiFi: bringup: transmit power level skipped/failed (me/mm already ok; see line above)\n");
    }
    SdhciMark("WiFi: bringup: rf calibrate NOT sent (Docs/20 §1e nocalib default)\n");
    /*
     * Docs/20 §1f：对表厂商在 ADD_IF 前取 efuse MAC。失败则保留默认 02:..:01。
     * 须在首次 MM_ADD_IF（scan/join）之前；本 bringup 末尾正是那个点。
     */
    (void)AicStationAddressLoadFromFirmware(Host);
    SdhciMark("WiFi: bringup ok (");
    SdhciMarkDecimal("", AIC_BRINGUP_STEP_COUNT);
    SdhciMark("/");
    SdhciMarkDecimal("", AIC_BRINGUP_STEP_COUNT);
    SdhciMark(")\n");
    return 0;
}

int SdhciWifiBringup(int Dry) {
    if (Dry != 0) {
        return BringupDry();
    }
    return BringupRun(SdhciHostAt(1));
}
