/*
 * WifiRfCalibrate.c — D80 MM_SET_RF_CALIB_REQ（对表厂商 rwnx_send_rf_calib_req）
 *
 * 对表 aicwf_set_rf_config_8800d80：先 txpwr_lvl_v3（WifiTransmitPowerLevel.c），
 * 再本消息。Bringup 放 me/mm 后软失败（ME 前发且无 CFM 会卡 MM，Docs/20 §1c/1d）。
 */
#include "WifiPrivate.h"
#include "CString.h"

#define AIC_RF_CALIBRATE_LENGTH 24u
#define AIC_RF_CALIBRATE_CONFIRM_LENGTH 16u
/* D80：rwnx_msg_tx.c:1049-1051 */
#define AIC_RF_CALIBRATE_CONFIG_24G 0x0f8fu
#define AIC_RF_CALIBRATE_CONFIG_5G 0x0f0fu
#define AIC_RF_CALIBRATE_PARAM_ALPHA 0x0c34c008u
#define AIC_RF_CALIBRATE_BLUETOOTH_PARAM 0x264203u

typedef struct {
    UINT32 CalibrateConfig24G;
    UINT32 CalibrateConfig5G;
    UINT32 ParameterAlpha;
    UINT32 BluetoothCalibrateEnable;
    UINT32 BluetoothCalibrateParameter;
    UINT8 CrystalCapacitance;
    UINT8 CrystalCapacitanceFine;
    UINT8 TailPad[2];
} AIC_MM_SET_RF_CALIBRATE;

static UINT32 sAicRfCalibrateBuffer[(AIC_RF_CALIBRATE_LENGTH + 3u) / 4u];
static UINT8 sAicRfCalibrateConfirm[AIC_RF_CALIBRATE_CONFIRM_LENGTH];

int AicWifiRfCalibrate(SDHCI_HOST *Host) {
    AIC_MM_SET_RF_CALIBRATE *Request = (AIC_MM_SET_RF_CALIBRATE *)sAicRfCalibrateBuffer;
    UINT32 ConfirmLength = 0;

    if (Host == 0) {
        return -1;
    }
    if ((UINT32)sizeof(AIC_MM_SET_RF_CALIBRATE) != AIC_RF_CALIBRATE_LENGTH) {
        SdhciMark("WiFi: rf calibrate layout BAD sizeof=");
        SdhciMarkDecimal("", (UINT32)sizeof(AIC_MM_SET_RF_CALIBRATE));
        SdhciMark("\n");
        return -1;
    }
    memset(Request, 0, sizeof(*Request));
    Request->CalibrateConfig24G = AIC_RF_CALIBRATE_CONFIG_24G;
    Request->CalibrateConfig5G = AIC_RF_CALIBRATE_CONFIG_5G;
    Request->ParameterAlpha = AIC_RF_CALIBRATE_PARAM_ALPHA;
    Request->BluetoothCalibrateEnable = 0u;
    Request->BluetoothCalibrateParameter = AIC_RF_CALIBRATE_BLUETOOTH_PARAM;
    SdhciMark("WiFi: rf calibrate req (MM_SET_RF_CALIB 0x69 cfg24=");
    SdhciMarkHex("", Request->CalibrateConfig24G, 4);
    SdhciMark(")\n");
    if (AicSendMessageEx(Host, (UINT16)AIC_MM_SET_RF_CALIBRATE_REQUEST, (UINT16)AIC_TASK_MM,
                         sAicRfCalibrateBuffer, (UINT16)AIC_RF_CALIBRATE_LENGTH) != 0) {
        SdhciMark("WiFi: rf calibrate FAILED: SDIO tx\n");
        return -1;
    }
    if (AicRecvMessageTimeout(Host, (UINT16)AIC_MM_SET_RF_CALIBRATE_CONFIRM,
                              sAicRfCalibrateConfirm, (UINT32)sizeof(sAicRfCalibrateConfirm),
                              &ConfirmLength, AIC_BRINGUP_CFM_TRIES, AIC_CFM_TIMEOUT_MS) != 0) {
        SdhciMark("WiFi: rf calibrate FAILED: no MM_SET_RF_CALIB_CFM(0x6a)\n");
        return -1;
    }
    SdhciMark("WiFi: rf calibrate ok (cfm len=");
    SdhciMarkDecimal("", ConfirmLength);
    SdhciMark(")\n");
    return 0;
}
