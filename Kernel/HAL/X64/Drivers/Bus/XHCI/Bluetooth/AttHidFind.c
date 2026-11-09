/*
 * AttHidFind.c — HID Primary Service / Characteristic / CCCD 查找
 */
#include "XHCI/Bluetooth/Private.h"
#define ATT_ERROR_RESPONSE 0x01u
#define ATT_FIND_BY_TYPE_VALUE_RESPONSE 0x07u
#define ATT_READ_BY_TYPE_RESPONSE 0x09u
#define ATT_FIND_INFORMATION_RESPONSE 0x05u
#define ATT_UUID_PRIMARY_SERVICE 0x2800u
#define ATT_UUID_CHARACTERISTIC 0x2803u
#define ATT_UUID_HID_SERVICE 0x1812u
#define ATT_UUID_REPORT 0x2A4Du
#define ATT_UUID_REPORT_MAP 0x2A4Bu
#define ATT_UUID_CLIENT_CHARACTERISTIC_CONFIGURATION 0x2902u
#define ATT_PROPERTY_NOTIFY 0x10u
#define ATT_ERROR_ATTRIBUTE_NOT_FOUND 0x0Au
#define ATT_STAGE_FIND_SERVICE 0x0011u
#define ATT_STAGE_FIND_CHARACTERISTIC 0x0012u
#define ATT_STAGE_ENABLE_NOTIFY 0x0013u
static int AttExpectOpcodeOrNotFound(UINT8 WantOpcode,
                                     UINT8 *AttributeProtocolDataUnitOut,
                                     UINT16 Capacity, UINT16 *LengthOut,
                                     UINT32 TimeoutMs, UINT16 Stage) {
    UINT64 Start = ReadTsc();
    UINT64 Need = (UINT64)TimeoutMs * 3000000ULL;
    while (ReadTsc() - Start < Need) {
        UINT16 GotLength = 0;
        UINT16 RemainingMilliseconds;
        UINT64 Elapsed = ReadTsc() - Start;
        if (Elapsed >= Need) {
            break;
        }
        RemainingMilliseconds = (UINT16)((Need - Elapsed) / 3000000ULL);
        if (RemainingMilliseconds < 50u) {
            RemainingMilliseconds = 50u;
        }
        if (BluetoothAttTake(AttributeProtocolDataUnitOut, Capacity, &GotLength,
                             RemainingMilliseconds) < 0) {
            continue;
        }
        if (AttributeProtocolDataUnitOut[0] == ATT_ERROR_RESPONSE) {
            UINT8 ErrorCode =
                GotLength > 4u ? AttributeProtocolDataUnitOut[4] : 0xFFu;
            if (ErrorCode == ATT_ERROR_ATTRIBUTE_NOT_FOUND) {
                return 1;
            }
            BluetoothLeLastErrorSet(Stage, ErrorCode);
            BootLogHex("bt: att Error code=", ErrorCode, 2);
            return -1;
        }
        if (AttributeProtocolDataUnitOut[0] == WantOpcode) {
            if (LengthOut != 0) {
                *LengthOut = GotLength;
            }
            return 0;
        }
    }
    BluetoothLeLastErrorSet(Stage, 0xFEu);
    return -1;
}
static int FindClientConfiguration(UINT16 ConnectionHandle, UINT16 StartHandle,
                                   UINT16 EndHandle, UINT16 *OutHandle,
                                   UINT32 TimeoutMs) {
    UINT16 Cursor = StartHandle;
    if (OutHandle == 0 || StartHandle == 0u || StartHandle > EndHandle) {
        return -1;
    }
    *OutHandle = 0;
    while (Cursor <= EndHandle) {
        UINT8 Response[48];
        UINT16 ResponseLength = 0;
        UINT8 Request[5];
        UINT8 Format;
        UINT16 PairSize;
        UINT16 Offset;
        int Expect;
        Request[0] = 0x04u;
        Request[1] = (UINT8)(Cursor & 0xFFu);
        Request[2] = (UINT8)((Cursor >> 8) & 0xFFu);
        Request[3] = (UINT8)(EndHandle & 0xFFu);
        Request[4] = (UINT8)((EndHandle >> 8) & 0xFFu);
        if (BluetoothAttSend(ConnectionHandle, Request, 5) < 0) {
            return -1;
        }
        Expect = AttExpectOpcodeOrNotFound(ATT_FIND_INFORMATION_RESPONSE,
                                           Response, sizeof(Response),
                                           &ResponseLength, TimeoutMs,
                                           ATT_STAGE_ENABLE_NOTIFY);
        if (Expect == 1) {
            return -1;
        }
        if (Expect < 0 || ResponseLength < 2u) {
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
            if (Uuid16 == ATT_UUID_CLIENT_CHARACTERISTIC_CONFIGURATION) {
                *OutHandle = AttributeHandle;
                return 0;
            }
            Cursor = (UINT16)(AttributeHandle + 1u);
        }
        if (Cursor <= StartHandle) {
            break;
        }
    }
    return -1;
}
int BluetoothAttHidFindService(UINT16 ConnectionHandle, UINT32 TimeoutMs,
                               UINT16 *ServiceStartOut, UINT16 *ServiceEndOut) {
    UINT8 HidUuidValue[2];
    UINT16 Cursor = 0x0001u;
    if (ServiceStartOut == 0 || ServiceEndOut == 0) {
        return -1;
    }
    *ServiceStartOut = 0;
    *ServiceEndOut = 0;
    HidUuidValue[0] = (UINT8)(ATT_UUID_HID_SERVICE & 0xFFu);
    HidUuidValue[1] = (UINT8)((ATT_UUID_HID_SERVICE >> 8) & 0xFFu);
    while (Cursor != 0u) {
        UINT8 Response[32];
        UINT16 ResponseLength = 0;
        UINT8 Request[9];
        int Expect;
        Request[0] = 0x06u;
        Request[1] = (UINT8)(Cursor & 0xFFu);
        Request[2] = (UINT8)((Cursor >> 8) & 0xFFu);
        Request[3] = 0xFFu;
        Request[4] = 0xFFu;
        Request[5] = (UINT8)(ATT_UUID_PRIMARY_SERVICE & 0xFFu);
        Request[6] = (UINT8)((ATT_UUID_PRIMARY_SERVICE >> 8) & 0xFFu);
        Request[7] = HidUuidValue[0];
        Request[8] = HidUuidValue[1];
        if (BluetoothAttSend(ConnectionHandle, Request, 9) < 0) {
            return -1;
        }
        Expect = AttExpectOpcodeOrNotFound(ATT_FIND_BY_TYPE_VALUE_RESPONSE,
                                           Response, sizeof(Response),
                                           &ResponseLength, TimeoutMs,
                                           ATT_STAGE_FIND_SERVICE);
        if (Expect == 1) {
            break;
        }
        if (Expect < 0 || ResponseLength < 5u) {
            return -1;
        }
        *ServiceStartOut =
            (UINT16)(Response[1] | ((UINT16)Response[2] << 8));
        *ServiceEndOut = (UINT16)(Response[3] | ((UINT16)Response[4] << 8));
        BootLogHex("bt: hid service start=", *ServiceStartOut, 4);
        BootLogHex(" end=", *ServiceEndOut, 4);
        return 0;
    }
    BootLog("bt: hid service not found\n");
    BluetoothLeLastErrorSet(ATT_STAGE_FIND_SERVICE, 0xFAu);
    return -1;
}
int BluetoothAttHidFindReports(UINT16 ConnectionHandle, UINT16 ServiceStart,
                               UINT16 ServiceEnd, UINT32 TimeoutMs,
                               UINT16 *ReportMapValueHandleOut,
                               UINT16 *ReportCountOut,
                               UINT16 *ReportValueHandles,
                               UINT16 *ReportClientConfigurationHandles,
                               UINT16 ReportCapacity) {
    UINT16 Cursor = ServiceStart;
    if (ReportMapValueHandleOut == 0 || ReportCountOut == 0 ||
        ReportValueHandles == 0 || ReportClientConfigurationHandles == 0 ||
        ReportCapacity == 0u) {
        return -1;
    }
    *ReportMapValueHandleOut = 0;
    *ReportCountOut = 0;
    while (Cursor != 0u && Cursor <= ServiceEnd) {
        UINT8 Response[48];
        UINT16 ResponseLength = 0;
        UINT8 Request[7];
        UINT8 AttributeDataLength;
        UINT16 Offset;
        int Expect;
        Request[0] = 0x08u;
        Request[1] = (UINT8)(Cursor & 0xFFu);
        Request[2] = (UINT8)((Cursor >> 8) & 0xFFu);
        Request[3] = (UINT8)(ServiceEnd & 0xFFu);
        Request[4] = (UINT8)((ServiceEnd >> 8) & 0xFFu);
        Request[5] = (UINT8)(ATT_UUID_CHARACTERISTIC & 0xFFu);
        Request[6] = (UINT8)((ATT_UUID_CHARACTERISTIC >> 8) & 0xFFu);
        if (BluetoothAttSend(ConnectionHandle, Request, 7) < 0) {
            return -1;
        }
        Expect = AttExpectOpcodeOrNotFound(ATT_READ_BY_TYPE_RESPONSE, Response,
                                           sizeof(Response), &ResponseLength,
                                           TimeoutMs,
                                           ATT_STAGE_FIND_CHARACTERISTIC);
        if (Expect == 1) {
            break;
        }
        if (Expect < 0 || ResponseLength < 4u) {
            return -1;
        }
        AttributeDataLength = Response[1];
        if (AttributeDataLength < 7u) {
            return -1;
        }
        for (Offset = 2u; Offset + AttributeDataLength <= ResponseLength;
             Offset = (UINT16)(Offset + AttributeDataLength)) {
            UINT16 DeclarationHandle =
                (UINT16)(Response[Offset] | ((UINT16)Response[Offset + 1u] << 8));
            UINT8 Properties = Response[Offset + 2u];
            UINT16 ValueHandle = (UINT16)(Response[Offset + 3u] |
                                          ((UINT16)Response[Offset + 4u] << 8));
            UINT16 Uuid16 = (UINT16)(Response[Offset + 5u] |
                                     ((UINT16)Response[Offset + 6u] << 8));
            Cursor = (UINT16)(DeclarationHandle + 1u);
            if (Uuid16 == ATT_UUID_REPORT_MAP) {
                *ReportMapValueHandleOut = ValueHandle;
                BootLogHex("bt: hid report-map handle=", ValueHandle, 4);
            }
            if (Uuid16 == ATT_UUID_REPORT && *ReportCountOut < ReportCapacity) {
                UINT16 Index = *ReportCountOut;
                ReportValueHandles[Index] = ValueHandle;
                ReportClientConfigurationHandles[Index] = 0;
                BootLogHex("bt: hid report handle=", ValueHandle, 4);
                BootLogHex(" prop=", Properties, 2);
                /*
                 * 只记 CCCD 句柄，不写 Enable——订阅属 BTM-7。
                 * 本刀若打开 Notify，MX 报告洪泛 ACL，易饿死有线/接收器键盘。
                 */
                if ((Properties & ATT_PROPERTY_NOTIFY) != 0u) {
                    UINT16 ConfigurationHandle = 0;
                    UINT16 SearchEnd = (UINT16)(ValueHandle + 2u);
                    if (SearchEnd > ServiceEnd) {
                        SearchEnd = ServiceEnd;
                    }
                    if (FindClientConfiguration(ConnectionHandle,
                                                (UINT16)(ValueHandle + 1u),
                                                SearchEnd, &ConfigurationHandle,
                                                TimeoutMs) == 0) {
                        ReportClientConfigurationHandles[Index] =
                            ConfigurationHandle;
                        BootLogHex("bt: hid cccd handle=", ConfigurationHandle,
                                   4);
                    }
                }
                (*ReportCountOut)++;
            }
        }
        if (Cursor <= ServiceStart) {
            break;
        }
    }
    if (*ReportCountOut == 0u) {
        BootLog("bt: hid report characteristic missing\n");
        BluetoothLeLastErrorSet(ATT_STAGE_FIND_CHARACTERISTIC, 0xFAu);
        return -1;
    }
    return 0;
}
