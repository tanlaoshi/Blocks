/*
 * HubRequest.c — hub 类请求（bd-pi-usb；对表 X64 Hub/Request）
 */
#include "UsbHostController/Private.h"

int HubGetPortStatus(UINT8 Hub, UINT8 Port, UINT16 *Status, UINT16 *Change) {
    UsbSetup Setup;
    UINT8 Buffer[4];
    int Ret;

    TransferTargetHub();
    Setup.BmRequestType = 0xA3;
    Setup.BRequest = 0;
    Setup.WValue = 0;
    Setup.WIndex = Port;
    Setup.WLength = 4;
    Ret = ControlMessage(Hub, &Setup, Buffer, 4);
    if (Ret < 4) {
        return -1;
    }
    *Status = (UINT16)(Buffer[0] | ((UINT16)Buffer[1] << 8));
    *Change = (UINT16)(Buffer[2] | ((UINT16)Buffer[3] << 8));
    return 0;
}

int HubSetPortFeature(UINT8 Hub, UINT8 Port, UINT16 Feature, int Set) {
    UsbSetup Setup;

    TransferTargetHub();
    Setup.BmRequestType = 0x23;
    Setup.BRequest = Set ? 0x03u : 0x01u;
    Setup.WValue = Feature;
    Setup.WIndex = Port;
    Setup.WLength = 0;
    return ControlMessage(Hub, &Setup, 0, 0) < 0 ? -1 : 0;
}

/* USB2 §11.24.2.3 CLEAR_TT_BUFFER：LS SPLIT 中段挂后清 TT，再重试控制传输 */
int HubClearTtBuffer(UINT8 Hub, UINT8 Port, UINT8 DeviceAddress, UINT8 EndpointNumber,
                            int EndpointIn) {
    UsbSetup Setup;

    TransferTargetHub();
    Setup.BmRequestType = 0x23;
    Setup.BRequest = 8;
    Setup.WValue = (UINT16)(((UINT16)DeviceAddress << 8) | (EndpointNumber & 0x0Fu) |
                            (EndpointIn ? 0x80u : 0u));
    Setup.WIndex = Port;
    Setup.WLength = 0;
    return ControlMessage(Hub, &Setup, 0, 0) < 0 ? -1 : 0;
}

int HubResetPort(UINT8 Hub, UINT8 Port, UINT16 *StatusOut) {
    UINT16 Status;
    UINT16 Change;
    UINT32 i;

    if (HubSetPortFeature(Hub, Port, HUB_FEAT_PORT_RESET, 1) != 0) {
        return -1;
    }
    for (i = 0; i < 50u; i++) {
        if (HubGetPortStatus(Hub, Port, &Status, &Change) != 0) {
            return -1;
        }
        if ((Status & HUB_STAT_RESET) == 0) {
            break;
        }
        Dwc2DelayMilliseconds(10);
    }
    (void)HubSetPortFeature(Hub, Port, HUB_FEAT_C_PORT_RESET, 0);
    (void)HubSetPortFeature(Hub, Port, HUB_FEAT_C_PORT_ENABLE, 0);
    Dwc2DelayMilliseconds(20);
    if (HubGetPortStatus(Hub, Port, &Status, &Change) != 0) {
        return -1;
    }
    if ((Status & HUB_STAT_ENABLE) == 0) {
        return -1;
    }
    *StatusOut = Status;
    return 0;
}

