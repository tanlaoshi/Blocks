/*
 * WifiEapolConfigFrame.c — EAPOL 轮询里的配置帧分流（从 WifiEapol.c 拆出）
 *
 * 识别 ME_SET_CONTROL_PORT_CFM / MM_KEY_ADD_CFM / MM_KEY_DEL_CFM，置位对应见过标志。
 */
#include "Wifi/EapolPrivate.h"
#include "Wifi/AssocPrivate.h"

UINT32 gAicEapolControlPortCfm;
UINT32 gAicEapolKeyAddCfm;
UINT32 gAicEapolKeyAddStatus;
UINT32 gAicEapolKeyAddHwIndex;
UINT32 gAicEapolKeyDeleteCfm;

/*
 * MM_KEY_ADD_CFM(0x25)：status + hw_key_idx。参数不够长只置"见过"。
 */
static void EapolRecordKeyAddCfm(const UINT8 *Frame, UINT32 Length, UINT16 ParameterLength) {
    gAicEapolKeyAddCfm = 1u;
    if (Length < AIC_E2A_PARAM_OFF + AIC_EAPOL_KEY_ADD_CFM_LENGTH ||
        ParameterLength < AIC_EAPOL_KEY_ADD_CFM_LENGTH) {
        gAicEapolKeyAddStatus = AIC_EAPOL_KEY_ADD_STATUS_SHORT;
        gAicEapolKeyAddHwIndex = 0u;
        return;
    }
    gAicEapolKeyAddStatus =
        (UINT32)Frame[AIC_E2A_PARAM_OFF + AIC_EAPOL_KEY_ADD_STATUS_OFFSET];
    gAicEapolKeyAddHwIndex =
        (UINT32)Frame[AIC_E2A_PARAM_OFF + AIC_EAPOL_KEY_ADD_HW_INDEX_OFFSET];
}

void AicEapolPrintConfigFrame(const UINT8 *Frame, UINT32 Length, AIC_EAPOL_COUNTERS *Counters) {
    UINT16 MessageId = 0;
    UINT16 ParameterLength = 0;

    Counters->ConfigFrames++;
    if (Length >= AIC_E2A_PARAM_OFF) {
        MessageId = AicGet16(Frame + 4);
        ParameterLength = AicGet16(Frame + 10);
    }
    SdhciMarkHex("WiFi: eapol: skip config frame type=0x", (UINT32)Frame[2], 2);
    SdhciMarkHex(" id=0x", MessageId, 4);
    SdhciMarkHex(" plen=", ParameterLength, 4);
    if (MessageId == AIC_SM_DISCONNECT_IND) {
        SdhciMark(" (SM_DISCONNECT_IND: assoc lost before M1)");
    } else if (MessageId == AIC_SM_CONNECT_IND) {
        SdhciMark(" (SM_CONNECT_IND)");
    } else if (MessageId == AIC_ME_SET_CONTROL_PORT_CFM) {
        SdhciMark(" (ME_SET_CONTROL_PORT_CFM: EAPOL control port is open)");
        gAicEapolControlPortCfm = 1u;
    } else if (MessageId == AIC_MM_KEY_ADD_CFM) {
        EapolRecordKeyAddCfm(Frame, Length, ParameterLength);
        SdhciMark(" (MM_KEY_ADD_CFM: status=");
        SdhciMarkDecimal("", gAicEapolKeyAddStatus);
        SdhciMark(" hw_key_idx=");
        SdhciMarkDecimal("", gAicEapolKeyAddHwIndex);
        SdhciMark(")");
    } else if (MessageId == AIC_MM_KEY_DELETE_CFM) {
        gAicEapolKeyDeleteCfm = 1u;
        SdhciMark(" (MM_KEY_DEL_CFM)");
    }
    SdhciMark("\n");
}
