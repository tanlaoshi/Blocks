/*
 * AttClient.c — LE ATT 请求/响应（L2CAP CID 0x0004）；鼠优先切片泵。
 * MTU 本刀钉 23（默认），避免 HCI ACL 分片未拼装时撑爆发现响应。
 */
#include "XHCI/Bluetooth/Private.h"
#include "XHCI.h"
#define L2CAP_CID_ATTRIBUTE 0x0004u
#define ATT_ERROR_RESPONSE 0x01u
#define ATT_EXCHANGE_MTU_REQUEST 0x02u
#define ATT_EXCHANGE_MTU_RESPONSE 0x03u
#define ATT_FIND_INFORMATION_REQUEST 0x04u
#define ATT_FIND_INFORMATION_RESPONSE 0x05u
#define ATT_FIND_BY_TYPE_VALUE_REQUEST 0x06u
#define ATT_FIND_BY_TYPE_VALUE_RESPONSE 0x07u
#define ATT_READ_BY_TYPE_REQUEST 0x08u
#define ATT_READ_BY_TYPE_RESPONSE 0x09u
#define ATT_WRITE_REQUEST 0x12u
#define ATT_WRITE_RESPONSE 0x13u
#define ATT_HANDLE_VALUE_NOTIFICATION 0x1Bu
#define ATT_HANDLE_VALUE_INDICATION 0x1Du
#define ATT_SLICE_MS 50u
#define ATT_DEFAULT_TIMEOUT_MS 10000u
#define ATT_DEFAULT_MAXIMUM_TRANSMISSION_UNIT 23u
static UINT16 sAttMaximumTransmissionUnit = ATT_DEFAULT_MAXIMUM_TRANSMISSION_UNIT;
static void AttPump(void) {
    if (HalCpuIsHypervisor()) {
        ProcessEvents();
    } else {
        ProcessEventsRealPc();
    }
    BluetoothHostPump();
    ServiceHidCompletions();
}
UINT16 BluetoothAttMaximumTransmissionUnitGet(void) {
    return sAttMaximumTransmissionUnit;
}
void BluetoothAttMaximumTransmissionUnitReset(void) {
    sAttMaximumTransmissionUnit = ATT_DEFAULT_MAXIMUM_TRANSMISSION_UNIT;
}
int BluetoothAttSend(UINT16 Handle, const UINT8 *AttributeProtocolDataUnit,
                     UINT16 Length) {
    UINT8 Frame[48];
    UINT16 Index;
    if (AttributeProtocolDataUnit == 0 || Length == 0u ||
        Length + 4u > sizeof(Frame)) {
        return -1;
    }
    Frame[0] = (UINT8)(Length & 0xFFu);
    Frame[1] = (UINT8)((Length >> 8) & 0xFFu);
    Frame[2] = (UINT8)(L2CAP_CID_ATTRIBUTE & 0xFFu);
    Frame[3] = (UINT8)((L2CAP_CID_ATTRIBUTE >> 8) & 0xFFu);
    for (Index = 0; Index < Length; Index++) {
        Frame[4u + Index] = AttributeProtocolDataUnit[Index];
    }
    return BluetoothAclOutSend(Handle, Frame, (UINT16)(4u + Length));
}
int BluetoothAttTake(UINT8 *AttributeProtocolDataUnitOut, UINT16 Capacity,
                     UINT16 *LengthOut, UINT32 TimeoutMs) {
    UINT64 Start = ReadTsc();
    UINT64 Need = (UINT64)TimeoutMs * 3000000ULL;
    if (TimeoutMs == 0u) {
        TimeoutMs = ATT_DEFAULT_TIMEOUT_MS;
        Need = (UINT64)TimeoutMs * 3000000ULL;
    }
    while (ReadTsc() - Start < Need) {
        UINT8 Acl[128];
        UINT16 AclLength = 0;
        UINT16 L2Length;
        UINT16 ChannelIdentifier;
        UINT16 Index;
        AttPump();
        while (BluetoothAclInboundTake(Acl, sizeof(Acl), &AclLength)) {
            if (AclLength < 5u) {
                continue;
            }
            L2Length = (UINT16)(Acl[0] | ((UINT16)Acl[1] << 8));
            ChannelIdentifier = (UINT16)(Acl[2] | ((UINT16)Acl[3] << 8));
            if (ChannelIdentifier != L2CAP_CID_ATTRIBUTE || L2Length == 0u ||
                (UINT16)(4u + L2Length) > AclLength) {
                continue;
            }
            if (L2Length > Capacity) {
                L2Length = Capacity;
            }
            for (Index = 0; Index < L2Length; Index++) {
                AttributeProtocolDataUnitOut[Index] = Acl[4u + Index];
            }
            if (LengthOut != 0) {
                *LengthOut = L2Length;
            }
            return 0;
        }
        /* 切片 Stall 已内含 ServiceHid；再泵一次 HCI，勿让 ACL 背压拖键 */
        BluetoothHostPump();
        ServiceHidCompletions();
        StallMilliseconds(ATT_SLICE_MS);
    }
    return -1;
}
int BluetoothAttExpect(UINT8 WantOpcode, UINT8 *AttributeProtocolDataUnitOut,
                       UINT16 Capacity, UINT16 *LengthOut, UINT32 TimeoutMs,
                       UINT16 Stage) {
    UINT64 Start = ReadTsc();
    UINT64 Need = (UINT64)TimeoutMs * 3000000ULL;
    if (TimeoutMs == 0u) {
        TimeoutMs = ATT_DEFAULT_TIMEOUT_MS;
        Need = (UINT64)TimeoutMs * 3000000ULL;
    }
    while (ReadTsc() - Start < Need) {
        UINT16 Left;
        UINT16 GotLength = 0;
        UINT64 Elapsed = ReadTsc() - Start;
        if (Elapsed >= Need) {
            break;
        }
        Left = (UINT16)((Need - Elapsed) / 3000000ULL);
        if (Left < 50u) {
            Left = 50u;
        }
        if (BluetoothAttTake(AttributeProtocolDataUnitOut, Capacity, &GotLength,
                             Left) < 0) {
            continue;
        }
        if (AttributeProtocolDataUnitOut[0] == ATT_ERROR_RESPONSE) {
            UINT8 ErrorCode =
                GotLength > 4u ? AttributeProtocolDataUnitOut[4] : 0xFFu;
            BluetoothLeLastErrorSet(Stage, ErrorCode);
            BootLogHex("bt: att Error code=", ErrorCode, 2);
            return -1;
        }
        if (AttributeProtocolDataUnitOut[0] == ATT_HANDLE_VALUE_NOTIFICATION ||
            AttributeProtocolDataUnitOut[0] == ATT_HANDLE_VALUE_INDICATION) {
            continue;
        }
        if (AttributeProtocolDataUnitOut[0] == WantOpcode) {
            if (LengthOut != 0) {
                *LengthOut = GotLength;
            }
            return 0;
        }
        BootLogHex("bt: att skip opcode=", AttributeProtocolDataUnitOut[0], 2);
    }
    BluetoothLeLastErrorSet(Stage, 0xFEu);
    return -1;
}
int BluetoothAttExchangeMaximumTransmissionUnit(UINT16 Handle,
                                                UINT16 ClientReceiveMaximum,
                                                UINT32 TimeoutMs) {
    UINT8 Request[3];
    UINT8 Response[8];
    UINT16 ResponseLength = 0;
    UINT16 ServerReceiveMaximum;
    if (ClientReceiveMaximum < ATT_DEFAULT_MAXIMUM_TRANSMISSION_UNIT) {
        ClientReceiveMaximum = ATT_DEFAULT_MAXIMUM_TRANSMISSION_UNIT;
    }
    /* 本刀上限 23：大 MTU 需 HCI ACL 续包拼 L2CAP，BTM-7+ 再开 */
    if (ClientReceiveMaximum > ATT_DEFAULT_MAXIMUM_TRANSMISSION_UNIT) {
        ClientReceiveMaximum = ATT_DEFAULT_MAXIMUM_TRANSMISSION_UNIT;
    }
    Request[0] = ATT_EXCHANGE_MTU_REQUEST;
    Request[1] = (UINT8)(ClientReceiveMaximum & 0xFFu);
    Request[2] = (UINT8)((ClientReceiveMaximum >> 8) & 0xFFu);
    if (BluetoothAttSend(Handle, Request, 3) < 0) {
        return -1;
    }
    if (BluetoothAttExpect(ATT_EXCHANGE_MTU_RESPONSE, Response, sizeof(Response),
                           &ResponseLength, TimeoutMs, 0x0010u) < 0 ||
        ResponseLength < 3u) {
        return -1;
    }
    ServerReceiveMaximum =
        (UINT16)(Response[1] | ((UINT16)Response[2] << 8));
    sAttMaximumTransmissionUnit = ClientReceiveMaximum;
    if (ServerReceiveMaximum < sAttMaximumTransmissionUnit) {
        sAttMaximumTransmissionUnit = ServerReceiveMaximum;
    }
    if (sAttMaximumTransmissionUnit < ATT_DEFAULT_MAXIMUM_TRANSMISSION_UNIT) {
        sAttMaximumTransmissionUnit = ATT_DEFAULT_MAXIMUM_TRANSMISSION_UNIT;
    }
    BootLogHex("bt: att mtu=", sAttMaximumTransmissionUnit, 4);
    return 0;
}
int BluetoothAttFindByTypeValue(UINT16 Handle, UINT16 StartHandle,
                                UINT16 EndHandle, UINT16 TypeUuid,
                                const UINT8 *Value, UINT16 ValueLength,
                                UINT8 *ResponseOut, UINT16 ResponseCapacity,
                                UINT16 *ResponseLengthOut, UINT32 TimeoutMs,
                                UINT16 Stage) {
    UINT8 Request[16];
    UINT16 Index;
    if (Value == 0 || ValueLength == 0u || ValueLength > 8u || ResponseOut == 0) {
        return -1;
    }
    Request[0] = ATT_FIND_BY_TYPE_VALUE_REQUEST;
    Request[1] = (UINT8)(StartHandle & 0xFFu);
    Request[2] = (UINT8)((StartHandle >> 8) & 0xFFu);
    Request[3] = (UINT8)(EndHandle & 0xFFu);
    Request[4] = (UINT8)((EndHandle >> 8) & 0xFFu);
    Request[5] = (UINT8)(TypeUuid & 0xFFu);
    Request[6] = (UINT8)((TypeUuid >> 8) & 0xFFu);
    for (Index = 0; Index < ValueLength; Index++) {
        Request[7u + Index] = Value[Index];
    }
    if (BluetoothAttSend(Handle, Request, (UINT16)(7u + ValueLength)) < 0) {
        return -1;
    }
    return BluetoothAttExpect(ATT_FIND_BY_TYPE_VALUE_RESPONSE, ResponseOut,
                              ResponseCapacity, ResponseLengthOut, TimeoutMs,
                              Stage);
}
int BluetoothAttReadByType(UINT16 Handle, UINT16 StartHandle, UINT16 EndHandle,
                           UINT16 TypeUuid, UINT8 *ResponseOut,
                           UINT16 ResponseCapacity, UINT16 *ResponseLengthOut,
                           UINT32 TimeoutMs, UINT16 Stage) {
    UINT8 Request[7];
    Request[0] = ATT_READ_BY_TYPE_REQUEST;
    Request[1] = (UINT8)(StartHandle & 0xFFu);
    Request[2] = (UINT8)((StartHandle >> 8) & 0xFFu);
    Request[3] = (UINT8)(EndHandle & 0xFFu);
    Request[4] = (UINT8)((EndHandle >> 8) & 0xFFu);
    Request[5] = (UINT8)(TypeUuid & 0xFFu);
    Request[6] = (UINT8)((TypeUuid >> 8) & 0xFFu);
    if (BluetoothAttSend(Handle, Request, 7) < 0) {
        return -1;
    }
    return BluetoothAttExpect(ATT_READ_BY_TYPE_RESPONSE, ResponseOut,
                              ResponseCapacity, ResponseLengthOut, TimeoutMs,
                              Stage);
}
int BluetoothAttFindInformation(UINT16 Handle, UINT16 StartHandle,
                                UINT16 EndHandle, UINT8 *ResponseOut,
                                UINT16 ResponseCapacity,
                                UINT16 *ResponseLengthOut, UINT32 TimeoutMs,
                                UINT16 Stage) {
    UINT8 Request[5];
    Request[0] = ATT_FIND_INFORMATION_REQUEST;
    Request[1] = (UINT8)(StartHandle & 0xFFu);
    Request[2] = (UINT8)((StartHandle >> 8) & 0xFFu);
    Request[3] = (UINT8)(EndHandle & 0xFFu);
    Request[4] = (UINT8)((EndHandle >> 8) & 0xFFu);
    if (BluetoothAttSend(Handle, Request, 5) < 0) {
        return -1;
    }
    return BluetoothAttExpect(ATT_FIND_INFORMATION_RESPONSE, ResponseOut,
                              ResponseCapacity, ResponseLengthOut, TimeoutMs,
                              Stage);
}
int BluetoothAttWriteRequest(UINT16 Handle, UINT16 AttributeHandle,
                             const UINT8 *Value, UINT16 ValueLength,
                             UINT32 TimeoutMs, UINT16 Stage) {
    UINT8 Request[24];
    UINT8 Response[4];
    UINT16 ResponseLength = 0;
    UINT16 Index;
    if (Value == 0 || ValueLength == 0u || ValueLength + 3u > sizeof(Request)) {
        return -1;
    }
    Request[0] = ATT_WRITE_REQUEST;
    Request[1] = (UINT8)(AttributeHandle & 0xFFu);
    Request[2] = (UINT8)((AttributeHandle >> 8) & 0xFFu);
    for (Index = 0; Index < ValueLength; Index++) {
        Request[3u + Index] = Value[Index];
    }
    if (BluetoothAttSend(Handle, Request, (UINT16)(3u + ValueLength)) < 0) {
        return -1;
    }
    return BluetoothAttExpect(ATT_WRITE_RESPONSE, Response, sizeof(Response),
                              &ResponseLength, TimeoutMs, Stage);
}
