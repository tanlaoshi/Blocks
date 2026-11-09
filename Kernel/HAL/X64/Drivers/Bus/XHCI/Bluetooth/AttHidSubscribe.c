/*
 * AttHidSubscribe.c — 读 Report Map；Protocol Mode=Report；订 Input Notify
 *
 * CCCD 是 GATT 订阅开关：写成 Notify 后外设持续上报，Linux/HOGP 也是常开。
 * 本刀首包 hexdump 后保持 CCCD；有线鼠被饿是 xHCI 泵优先级问题，禁止用卸订/断 LE 掩盖。
 */
#include "XHCI/Bluetooth/Private.h"
#include "XHCI.h"
#define ATT_UUID_REPORT_REFERENCE 0x2908u
#define ATT_REPORT_TYPE_INPUT 0x01u
#define ATT_HID_REPORT_MAX 8u
#define ATT_REPORT_MAP_MAX 512u
#define ATT_STAGE_READ_MAP 0x0020u
#define ATT_STAGE_ENABLE_NOTIFY 0x0021u
#define ATT_STAGE_WAIT_NOTIFY 0x0022u
#define ATT_DEFAULT_MS 20000u
#define ATT_NOTIFY_TRY_MS 2500u
static UINT8 sReportMap[ATT_REPORT_MAP_MAX];
static UINT16 sReportMapLength;
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
static void HexDumpPrefix(const char *Prefix, const UINT8 *Data, UINT16 Length,
                          UINT16 MaxShow) {
    UINT16 Index;
    UINT16 Show = Length;
    if (Show > MaxShow) {
        Show = MaxShow;
    }
    BootLog(Prefix ? Prefix : "bt: hex=");
    for (Index = 0; Index < Show; Index++) {
        BootLogHex(Index == 0u ? "" : " ", Data[Index], 2);
    }
    BootLog(Length > Show ? "...\n" : "\n");
}
static void DisarmInputNotifications(UINT16 ConnectionHandle) {
    BluetoothAttHidDisarmClientConfiguration(
        ConnectionHandle, sEnabledClientConfigurationHandle,
        sEnabledClientConfigurationCount);
    sEnabledClientConfigurationCount = 0;
}
static int SetProtocolModeReport(UINT16 ConnectionHandle, UINT32 TimeoutMs) {
    UINT16 ProtocolModeHandle = 0;
    UINT8 Mode[1];
    if (BluetoothAttHidDiscoveryGetProtocolMode(&ProtocolModeHandle) < 0 ||
        ProtocolModeHandle == 0u) {
        BootLog("bt: hid protocol-mode missing (continue)\n");
        return 0;
    }
    Mode[0] = 0x01u; /* Report Protocol Mode */
    if (BluetoothAttWriteRequest(ConnectionHandle, ProtocolModeHandle, Mode, 1,
                                 TimeoutMs, ATT_STAGE_ENABLE_NOTIFY) < 0) {
        /* 有的外设只接受 Write Command */
        if (BluetoothAttWriteCommand(ConnectionHandle, ProtocolModeHandle, Mode,
                                     1) < 0) {
            BootLog("bt: hid protocol-mode write fail\n");
            return -1;
        }
    }
    BootLog("bt: hid protocol-mode=report\n");
    BluetoothRequeueHid();
    return 0;
}
static int WaitFirstNotification(UINT32 TimeoutMs) {
    UINT64 Start = ReadTsc();
    UINT64 Need = (UINT64)TimeoutMs * 3000000ULL;
    UINT8 AttributeProtocolDataUnit[32];
    UINT16 Length = 0;
    BootLog("bt: notify move MX Master now\n");
    while (ReadTsc() - Start < Need) {
        if (HalCpuIsHypervisor()) {
            ProcessEvents();
        } else {
            ProcessEventsRealPc();
        }
        BluetoothHostPump();
        ServiceHidCompletions();
        BluetoothRequeueHid();
        if (BluetoothAttNotifyQueueTake(AttributeProtocolDataUnit,
                                        sizeof(AttributeProtocolDataUnit),
                                        &Length)) {
            if (Length >= 4u) {
                UINT16 AttributeHandle = (UINT16)(
                    AttributeProtocolDataUnit[1] |
                    ((UINT16)AttributeProtocolDataUnit[2] << 8));
                BootLogHex("bt: notify handle=", AttributeHandle, 4);
                HexDumpPrefix("bt: notify first=",
                              &AttributeProtocolDataUnit[3],
                              (UINT16)(Length - 3u), 16u);
            }
            return 0;
        }
        StallMilliseconds(10u);
    }
    return -1;
}
static int TryInputNotifications(UINT16 ConnectionHandle, UINT32 TimeoutMs) {
    UINT16 ServiceEnd = 0;
    UINT16 ReportCount = 0;
    const UINT16 *ReportValueHandles = 0;
    const UINT16 *ClientConfigurationHandles = 0;
    UINT16 Index;
    UINT16 Tried = 0;
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
        BluetoothAttNotifyQueueFlush();
        sEnabledClientConfigurationCount = 0;
        Enable[0] = 0x01u;
        Enable[1] = 0x00u;
        if (BluetoothAttWriteRequest(ConnectionHandle, ConfigurationHandle,
                                     Enable, 2, TimeoutMs,
                                     ATT_STAGE_ENABLE_NOTIFY) < 0) {
            continue;
        }
        sEnabledClientConfigurationHandle[0] = ConfigurationHandle;
        sEnabledClientConfigurationCount = 1;
        Tried++;
        BootLogHex("bt: hid notify try cccd=", ConfigurationHandle, 4);
        BluetoothAttNotifyAcceptSet(1);
        BluetoothRequeueHid();
        if (WaitFirstNotification(ATT_NOTIFY_TRY_MS) == 0) {
            BluetoothHidInputNoteArmed(ValueHandle, ReportId);
            return 0;
        }
        DisarmInputNotifications(ConnectionHandle);
        BootLog("bt: hid notify try miss; next\n");
    }
    if (Tried == 0u) {
        BootLog("bt: hid no input cccd to try\n");
        BluetoothLeLastErrorSet(ATT_STAGE_ENABLE_NOTIFY, 0xFAu);
    } else {
        BluetoothLeLastErrorSet(ATT_STAGE_WAIT_NOTIFY, 0xFEu);
        BootLog("bt: notify wait timeout (all input tried)\n");
    }
    return -1;
}
int XhciBluetoothAttHidSubscribe(UINT32 TimeoutMs) {
    UINT16 ConnectionHandle = 0;
    UINT16 ReportMapHandle = 0;
    if (TimeoutMs == 0u) {
        TimeoutMs = ATT_DEFAULT_MS;
    }
    BluetoothLeLastErrorSet(0, 0);
    sReportMapLength = 0;
    sEnabledClientConfigurationCount = 0;
    BluetoothHidInputNoteArmed(0, 0);
    BluetoothAttNotifyAcceptSet(0);
    BluetoothAttNotifyQueueFlush();
    if (BluetoothLeConnectionHandleGet(&ConnectionHandle) < 0) {
        BluetoothLeLastErrorSet(ATT_STAGE_READ_MAP, 0xFDu);
        return -1;
    }
    if (BluetoothAttHidDiscoveryGet(0, 0, &ReportMapHandle, 0, 0) < 0) {
        BootLog("bt: notify need hogp first\n");
        BluetoothLeLastErrorSet(ATT_STAGE_READ_MAP, 0xFDu);
        return -1;
    }
    if (!BluetoothHostPumpIsActive()) {
        return -1;
    }
    if (ReportMapHandle != 0u) {
        if (BluetoothAttReadLong(ConnectionHandle, ReportMapHandle, sReportMap,
                                 ATT_REPORT_MAP_MAX, &sReportMapLength,
                                 TimeoutMs, ATT_STAGE_READ_MAP) < 0) {
            BootLog("bt: report-map read fail\n");
            return -1;
        }
        BootLogHex("bt: report-map len=", sReportMapLength, 4);
        HexDumpPrefix("bt: report-map=", sReportMap, sReportMapLength, 16u);
    }
    if (SetProtocolModeReport(ConnectionHandle, TimeoutMs) < 0) {
        return -1;
    }
    if (TryInputNotifications(ConnectionHandle, TimeoutMs) < 0) {
        DisarmInputNotifications(ConnectionHandle);
        BluetoothAttHidRecoverWiredHid();
        return -1;
    }
    /* 订住 CCCD；若试订期间曾双投递打坏鼠环，Sync 后单次武装恢复 */
    BluetoothAttNotifyAcceptSet(1);
    BluetoothAttHidRecoverWiredHid();
    BootLog("bt: notify ok (cccd armed)\n");
    return 0;
}
int BluetoothAttHidReportMapGet(const UINT8 **DataOut, UINT16 *LengthOut) {
    if (sReportMapLength == 0u) {
        return -1;
    }
    if (DataOut != 0) {
        *DataOut = sReportMap;
    }
    if (LengthOut != 0) {
        *LengthOut = sReportMapLength;
    }
    return 0;
}
