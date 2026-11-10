/*
 * AttHidSubscribeTry.c — 试订 Input CCCD（PR-BTM-7/10 · PR-BT-HOGP-1）
 *
 * CCCD 写成功即武装；勿 WaitFirstNotification 逼连时晃鼠。
 * 多路 Input：全部开 Notify；错句柄由 HidInputBackend 见可解报告再改武装。
 */
#include "XHCI/Bluetooth/Private.h"
#include "XHCI.h"

#define ATT_UUID_REPORT_REFERENCE 0x2908u
#define ATT_REPORT_TYPE_INPUT 0x01u
#define ATT_HID_REPORT_MAX 8u
#define ATT_STAGE_ENABLE_NOTIFY 0x0021u

static UINT16 sEnabledClientConfigurationHandle[ATT_HID_REPORT_MAX];
static UINT16 sEnabledClientConfigurationCount;

static int FindReportReference(UINT16 ConnectionHandle, UINT16 StartHandle,
                               UINT16 EndHandle, UINT16 *ReferenceHandleOut,
                               UINT32 TimeoutMs) {
    UINT8 Response[48];
    UINT16 ResponseLength = 0;
    UINT8 Format;
    UINT16 PairSize;
    UINT16 Offset;

    if (ReferenceHandleOut == 0 || StartHandle == 0u || StartHandle > EndHandle) {
        return -1;
    }
    *ReferenceHandleOut = 0;
    if (BluetoothAttFindInformation(ConnectionHandle, StartHandle, EndHandle,
                                    Response, sizeof(Response), &ResponseLength,
                                    TimeoutMs, ATT_STAGE_ENABLE_NOTIFY) < 0) {
        return -1;
    }
    if (ResponseLength < 2u) {
        return -1;
    }
    Format = Response[1];
    PairSize = (Format == 0x01u) ? 4u : 18u;
    for (Offset = 2u; Offset + PairSize <= ResponseLength;
         Offset = (UINT16)(Offset + PairSize)) {
        UINT16 AttributeHandle =
            (UINT16)(Response[Offset] | ((UINT16)Response[Offset + 1u] << 8));
        UINT16 Uuid16 = 0;

        if (Format == 0x01u) {
            Uuid16 = (UINT16)(Response[Offset + 2u] |
                              ((UINT16)Response[Offset + 3u] << 8));
        }
        if (Uuid16 == ATT_UUID_REPORT_REFERENCE) {
            *ReferenceHandleOut = AttributeHandle;
            return 0;
        }
    }
    return -1;
}

static int ReportIsInput(UINT16 ConnectionHandle, UINT16 ValueHandle,
                         UINT16 ServiceEnd, UINT32 TimeoutMs, int *IsInputOut,
                         UINT8 *ReportIdOut) {
    UINT16 ReferenceHandle = 0;
    UINT8 Value[4];
    UINT16 ValueLength = 0;
    UINT16 SearchEnd = (UINT16)(ValueHandle + 4u);

    if (IsInputOut == 0) {
        return -1;
    }
    *IsInputOut = 0;
    if (ReportIdOut != 0) {
        *ReportIdOut = 0;
    }
    if (SearchEnd > ServiceEnd) {
        SearchEnd = ServiceEnd;
    }
    if (FindReportReference(ConnectionHandle, (UINT16)(ValueHandle + 1u),
                            SearchEnd, &ReferenceHandle, TimeoutMs) < 0) {
        return 1;
    }
    if (BluetoothAttReadRequest(ConnectionHandle, ReferenceHandle, Value,
                                sizeof(Value), &ValueLength, TimeoutMs,
                                ATT_STAGE_ENABLE_NOTIFY) < 0 ||
        ValueLength < 2u) {
        return -1;
    }
    if (ReportIdOut != 0) {
        *ReportIdOut = Value[0];
    }
    if (Value[1] == ATT_REPORT_TYPE_INPUT) {
        *IsInputOut = 1;
    }
    BootLogHex("bt: hid report-id=", Value[0], 2);
    BootLogHex(" type=", Value[1], 2);
    return 0;
}

static void DisarmInputNotifications(UINT16 ConnectionHandle) {
    BluetoothAttHidDisarmClientConfiguration(
        ConnectionHandle, sEnabledClientConfigurationHandle,
        sEnabledClientConfigurationCount);
    sEnabledClientConfigurationCount = 0;
}

int BluetoothAttHidTryInputNotifications(UINT16 ConnectionHandle,
                                         UINT32 TimeoutMs) {
    UINT16 ServiceEnd = 0;
    UINT16 ReportCount = 0;
    const UINT16 *ReportValueHandles = 0;
    const UINT16 *ClientConfigurationHandles = 0;
    UINT16 Index;
    UINT16 Tried = 0;
    int Armed = 0;

    sEnabledClientConfigurationCount = 0;
    if (BluetoothAttHidDiscoveryGetClientConfiguration(
            0, &ServiceEnd, &ReportCount, &ReportValueHandles,
            &ClientConfigurationHandles) < 0) {
        BluetoothLeLastErrorSet(ATT_STAGE_ENABLE_NOTIFY, 0xFDu);
        return -1;
    }
    for (Index = 0; Index < ReportCount && Index < ATT_HID_REPORT_MAX; Index++) {
        UINT16 ConfigurationHandle = ClientConfigurationHandles[Index];
        UINT16 ValueHandle = ReportValueHandles[Index];
        int IsInput = 0;
        int ReferenceResult;
        UINT8 ReportId = 0;
        UINT8 Enable[2];

        if (ConfigurationHandle == 0u) {
            continue;
        }
        ReferenceResult =
            ReportIsInput(ConnectionHandle, ValueHandle, ServiceEnd, TimeoutMs,
                          &IsInput, &ReportId);
        if (ReferenceResult < 0) {
            continue;
        }
        if (ReferenceResult == 1) {
            IsInput = 1;
            ReportId = 0;
        }
        if (!IsInput) {
            continue;
        }
        Enable[0] = 0x01u;
        Enable[1] = 0x00u;
        if (BluetoothAttWriteRequest(ConnectionHandle, ConfigurationHandle,
                                     Enable, 2, TimeoutMs,
                                     ATT_STAGE_ENABLE_NOTIFY) < 0) {
            continue;
        }
        if (sEnabledClientConfigurationCount < ATT_HID_REPORT_MAX) {
            sEnabledClientConfigurationHandle[sEnabledClientConfigurationCount] =
                ConfigurationHandle;
            sEnabledClientConfigurationCount++;
        }
        Tried++;
        BootLogHex("bt: hid notify cccd=", ConfigurationHandle, 4);
        BootLogHex(" value=", ValueHandle, 4);
        if (!Armed) {
            /*
             * PR-BT-HOGP-1：CCCD 成即武装；首包稍后跟手。
             * 其它 Input CCCD 仍开着，错句柄见 HidInputBackend 改武装。
             */
            BluetoothAttNotifyAcceptSet(1);
            BluetoothRequeueHid();
            BluetoothHidInputNoteArmed(ValueHandle, ReportId);
            Armed = 1;
        }
    }
    if (Armed) {
        BootLog("bt: hid notify armed (no wait-first)\n");
        return 0;
    }
    if (Tried == 0u) {
        BootLog("bt: hid no input cccd to try\n");
        BluetoothLeLastErrorSet(ATT_STAGE_ENABLE_NOTIFY, 0xFAu);
    } else {
        BluetoothLeLastErrorSet(ATT_STAGE_ENABLE_NOTIFY, 0xFEu);
        BootLog("bt: hid notify cccd write fail (all input)\n");
    }
    return -1;
}

void BluetoothAttHidDisarmEnabledNotifications(UINT16 ConnectionHandle) {
    DisarmInputNotifications(ConnectionHandle);
}
