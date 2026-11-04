/*
 * WifiTransmitPowerLevel.c — D80 MM_SET_TXPWR_IDX_LVL（txpwr_lvl_v3）
 *
 * 对表厂商 aicwf_set_rf_config_8800d80 → rwnx_send_txpwr_lvl_v3_req。
 * 表值取 Duo overlay aic_userconfig_8800d80.txt（真机 Linux 实测载入后同值）；
 * adj/ofst2x 在该 userconfig 里 enable=0，厂商直接 skip，本刀不发。
 */
#include "Wifi/Private.h"
#include "CString.h"

#define AIC_TRANSMIT_POWER_LEVEL_LENGTH 69u
#define AIC_TRANSMIT_POWER_LEVEL_CONFIRM_LENGTH 16u
/* 11a 5G 前 4 槽：厂商默认 NA = 0x80（s8 -128）；userconfig 只写 6M 起 */
#define AIC_TRANSMIT_POWER_LEVEL_NOT_APPLICABLE ((INT8)0x80)

typedef struct {
    UINT8 Enable;
    INT8 PowerLevel11b11ag24G[12];
    INT8 PowerLevel11n11ac24G[10];
    INT8 PowerLevel11ax24G[12];
    INT8 PowerLevel11a5G[12];
    INT8 PowerLevel11n11ac5G[10];
    INT8 PowerLevel11ax5G[12];
} AIC_TRANSMIT_POWER_LEVEL_VERSION3;

static UINT32 sAicTransmitPowerLevelBuffer[(AIC_TRANSMIT_POWER_LEVEL_LENGTH + 3u) / 4u];
static UINT8 sAicTransmitPowerLevelConfirm[AIC_TRANSMIT_POWER_LEVEL_CONFIRM_LENGTH];

/*
 * Duo aic_userconfig_8800d80.txt 展开（与 rwnx_platform 解析槽位一致）。
 * 11a_5g[0..3] 文件无键 → 保留厂商 NA。
 */
static void TransmitPowerLevelFillUserconfig(AIC_TRANSMIT_POWER_LEVEL_VERSION3 *Request) {
    static const INT8 Level11b11ag24G[12] = {
        18, 18, 18, 18, 18, 18, 18, 18, 16, 16, 15, 15};
    static const INT8 Level11n11ac24G[10] = {
        18, 18, 18, 18, 16, 16, 15, 15, 14, 14};
    static const INT8 Level11ax24G[12] = {
        18, 18, 18, 18, 16, 16, 15, 15, 14, 14, 13, 13};
    static const INT8 Level11a5G[12] = {
        AIC_TRANSMIT_POWER_LEVEL_NOT_APPLICABLE,
        AIC_TRANSMIT_POWER_LEVEL_NOT_APPLICABLE,
        AIC_TRANSMIT_POWER_LEVEL_NOT_APPLICABLE,
        AIC_TRANSMIT_POWER_LEVEL_NOT_APPLICABLE,
        18, 18, 18, 18, 16, 16, 15, 15};
    static const INT8 Level11n11ac5G[10] = {
        18, 18, 18, 18, 16, 16, 15, 15, 14, 14};
    static const INT8 Level11ax5G[12] = {
        18, 18, 18, 18, 16, 16, 14, 14, 13, 13, 12, 12};

    memset(Request, 0, sizeof(*Request));
    Request->Enable = 1u;
    memcpy(Request->PowerLevel11b11ag24G, Level11b11ag24G, sizeof(Level11b11ag24G));
    memcpy(Request->PowerLevel11n11ac24G, Level11n11ac24G, sizeof(Level11n11ac24G));
    memcpy(Request->PowerLevel11ax24G, Level11ax24G, sizeof(Level11ax24G));
    memcpy(Request->PowerLevel11a5G, Level11a5G, sizeof(Level11a5G));
    memcpy(Request->PowerLevel11n11ac5G, Level11n11ac5G, sizeof(Level11n11ac5G));
    memcpy(Request->PowerLevel11ax5G, Level11ax5G, sizeof(Level11ax5G));
}

int AicWifiTransmitPowerLevelSend(SDHCI_HOST *Host) {
    AIC_TRANSMIT_POWER_LEVEL_VERSION3 *Request =
        (AIC_TRANSMIT_POWER_LEVEL_VERSION3 *)sAicTransmitPowerLevelBuffer;
    UINT32 ConfirmLength = 0;

    if (Host == 0) {
        return -1;
    }
    if ((UINT32)sizeof(AIC_TRANSMIT_POWER_LEVEL_VERSION3) != AIC_TRANSMIT_POWER_LEVEL_LENGTH) {
        SdhciMark("WiFi: transmit power level layout BAD sizeof=");
        SdhciMarkDecimal("", (UINT32)sizeof(AIC_TRANSMIT_POWER_LEVEL_VERSION3));
        SdhciMark("\n");
        return -1;
    }
    TransmitPowerLevelFillUserconfig(Request);
    SdhciMark("WiFi: transmit power level req (MM_SET_TXPWR_IDX_LVL 0x77 enable=");
    SdhciMarkDecimal("", (UINT32)Request->Enable);
    SdhciMark(" 2g4_1m=");
    SdhciMarkDecimal("", (UINT32)(UINT8)Request->PowerLevel11b11ag24G[0]);
    SdhciMark(")\n");
    if (AicSendMessageEx(Host, (UINT16)AIC_MM_SET_TRANSMIT_POWER_LEVEL_REQUEST,
                         (UINT16)AIC_TASK_MM, sAicTransmitPowerLevelBuffer,
                         (UINT16)AIC_TRANSMIT_POWER_LEVEL_LENGTH) != 0) {
        SdhciMark("WiFi: transmit power level FAILED: SDIO tx\n");
        return -1;
    }
    if (AicRecvMessageTimeout(Host, (UINT16)AIC_MM_SET_TRANSMIT_POWER_LEVEL_CONFIRM,
                              sAicTransmitPowerLevelConfirm,
                              (UINT32)sizeof(sAicTransmitPowerLevelConfirm),
                              &ConfirmLength, AIC_BRINGUP_CFM_TRIES, AIC_CFM_TIMEOUT_MS) != 0) {
        SdhciMark("WiFi: transmit power level FAILED: no MM_SET_TXPWR_IDX_LVL_CFM(0x78)\n");
        return -1;
    }
    SdhciMark("WiFi: transmit power level ok (cfm len=");
    SdhciMarkDecimal("", ConfirmLength);
    SdhciMark(")\n");
    return 0;
}
