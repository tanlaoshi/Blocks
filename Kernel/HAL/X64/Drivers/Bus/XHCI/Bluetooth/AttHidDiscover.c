/*
 * AttHidDiscover.c — HOGP 发现门面：MTU → HID Service → Report + Notify
 */
#include "XHCI/Bluetooth/Private.h"
#include "XHCI.h"
#define ATT_HID_REPORT_MAX 8u
#define ATT_STAGE_FIND_SERVICE 0x0011u
#define ATT_DEFAULT_MS 15000u
typedef struct {
    UINT16 ServiceStartHandle;
    UINT16 ServiceEndHandle;
    UINT16 ReportMapValueHandle;
    UINT16 ProtocolModeValueHandle;
    UINT16 ReportCount;
    UINT16 ReportValueHandle[ATT_HID_REPORT_MAX];
    UINT16 ReportClientConfigurationHandle[ATT_HID_REPORT_MAX];
    int Ready;
} BluetoothAttHidDiscoveryState;
static BluetoothAttHidDiscoveryState sHidState;
void BluetoothAttHidDiscoveryClear(void) {
    UINT32 Index;
    sHidState.Ready = 0;
    sHidState.ServiceStartHandle = 0;
    sHidState.ServiceEndHandle = 0;
    sHidState.ReportMapValueHandle = 0;
    sHidState.ProtocolModeValueHandle = 0;
    sHidState.ReportCount = 0;
    for (Index = 0; Index < ATT_HID_REPORT_MAX; Index++) {
        sHidState.ReportValueHandle[Index] = 0;
        sHidState.ReportClientConfigurationHandle[Index] = 0;
    }
}
int BluetoothAttHidDiscoveryGet(UINT16 *ServiceStartOut, UINT16 *ServiceEndOut,
                                UINT16 *ReportMapHandleOut,
                                UINT16 *ReportCountOut,
                                const UINT16 **ReportValueHandlesOut) {
    if (!sHidState.Ready) {
        return -1;
    }
    if (ServiceStartOut != 0) {
        *ServiceStartOut = sHidState.ServiceStartHandle;
    }
    if (ServiceEndOut != 0) {
        *ServiceEndOut = sHidState.ServiceEndHandle;
    }
    if (ReportMapHandleOut != 0) {
        *ReportMapHandleOut = sHidState.ReportMapValueHandle;
    }
    if (ReportCountOut != 0) {
        *ReportCountOut = sHidState.ReportCount;
    }
    if (ReportValueHandlesOut != 0) {
        *ReportValueHandlesOut = sHidState.ReportValueHandle;
    }
    return 0;
}
int BluetoothAttHidDiscoveryGetClientConfiguration(
    UINT16 *ServiceStartOut, UINT16 *ServiceEndOut, UINT16 *ReportCountOut,
    const UINT16 **ReportValueHandlesOut,
    const UINT16 **ClientConfigurationHandlesOut) {
    if (!sHidState.Ready) {
        return -1;
    }
    if (ServiceStartOut != 0) {
        *ServiceStartOut = sHidState.ServiceStartHandle;
    }
    if (ServiceEndOut != 0) {
        *ServiceEndOut = sHidState.ServiceEndHandle;
    }
    if (ReportCountOut != 0) {
        *ReportCountOut = sHidState.ReportCount;
    }
    if (ReportValueHandlesOut != 0) {
        *ReportValueHandlesOut = sHidState.ReportValueHandle;
    }
    if (ClientConfigurationHandlesOut != 0) {
        *ClientConfigurationHandlesOut = sHidState.ReportClientConfigurationHandle;
    }
    return 0;
}
int BluetoothAttHidDiscoveryGetProtocolMode(UINT16 *ProtocolModeHandleOut) {
    if (!sHidState.Ready) {
        return -1;
    }
    if (ProtocolModeHandleOut != 0) {
        *ProtocolModeHandleOut = sHidState.ProtocolModeValueHandle;
    }
    return 0;
}
int XhciBluetoothAttHidDiscover(UINT32 TimeoutMs) {
    UINT16 ConnectionHandle = 0;
    if (TimeoutMs == 0u) {
        TimeoutMs = ATT_DEFAULT_MS;
    }
    BluetoothLeLastErrorSet(0, 0);
    BluetoothAttHidDiscoveryClear();
    BluetoothAttMaximumTransmissionUnitReset();
    if (BluetoothLeConnectionHandleGet(&ConnectionHandle) < 0) {
        BootLog("bt: att need le connect\n");
        BluetoothLeLastErrorSet(ATT_STAGE_FIND_SERVICE, 0xFDu);
        return -1;
    }
    if (!BluetoothHostPumpIsActive()) {
        return -1;
    }
    BluetoothHostEventQueueFlush();
    BluetoothAclInboundFlush();
    if (BluetoothAclFlowInitialize() < 0) {
        BluetoothLeLastErrorSet(ATT_STAGE_FIND_SERVICE, 0xFCu);
        return -1;
    }
    if (BluetoothAttExchangeMaximumTransmissionUnit(ConnectionHandle, 23u,
                                                    TimeoutMs) < 0) {
        BootLog("bt: att mtu fail\n");
        return -1;
    }
    if (BluetoothAttHidFindService(ConnectionHandle, TimeoutMs,
                                   &sHidState.ServiceStartHandle,
                                   &sHidState.ServiceEndHandle) < 0) {
        return -1;
    }
    if (BluetoothAttHidFindReports(
            ConnectionHandle, sHidState.ServiceStartHandle,
            sHidState.ServiceEndHandle, TimeoutMs,
            &sHidState.ReportMapValueHandle, &sHidState.ProtocolModeValueHandle,
            &sHidState.ReportCount, sHidState.ReportValueHandle,
            sHidState.ReportClientConfigurationHandle, ATT_HID_REPORT_MAX) < 0) {
        return -1;
    }
    sHidState.Ready = 1;
    BootLogHex("bt: hogp discover ok reports=", sHidState.ReportCount, 2);
    BluetoothRequeueHid();
    return 0;
}
