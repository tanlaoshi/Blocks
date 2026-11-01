/*
 * HciCommand.c — HCI 命令收发（H4 之上的最小子集）
 *
 * 发一条命令，等 Command Complete(0x0E)，取 status + 可选 return 参数。
 * 参考：Core Spec Vol 4 Part E；NUC Bluetooth/Hci.c 判据语义。
 *
 * 别改：收包超时返回错误码，不吞不猜；只认 Command Complete，
 * 不把 Command Status 当成功。
 */
#include "BluetoothPrivate.h"
#include "Hal.h"

int PiHciSendCommandWithTimeout(UINT16 Opcode, const UINT8 *Parameters,
                                UINT8 ParameterLength, UINT8 *OutStatus,
                                UINT8 *OutReturn, UINT8 ReturnMax,
                                UINT8 *OutReturnLength, UINT32 TimeoutMs) {
    UINT8 Header[4];
    UINT32 Index;
    UINT32 Start;
    UINT8 PacketType = 0u;
    UINT8 EventCode = 0u;
    UINT8 EventLength = 0u;
    UINT8 Event[PI_HCI_EVENT_MAX];
    UINT8 ReturnLength = 0u;

    if (OutStatus == 0) {
        return -1;
    }
    *OutStatus = 0xFFu;
    if (OutReturnLength != 0) {
        *OutReturnLength = 0u;
    }
    Header[0] = PI_HCI_PACKET_COMMAND;
    Header[1] = (UINT8)(Opcode & 0xFFu);
    Header[2] = (UINT8)(Opcode >> 8);
    Header[3] = ParameterLength;
    for (Index = 0u; Index < sizeof(Header); Index++) {
        PiUartWriteByte(Header[Index]);
    }
    for (Index = 0u; Index < ParameterLength; Index++) {
        PiUartWriteByte(Parameters[Index]);
    }

    Start = HalMilliseconds();
    for (;;) {
        if ((UINT32)(HalMilliseconds() - Start) >= TimeoutMs) {
            return -2;
        }
        if (PiUartReadByte(&PacketType, 20u) != 0) {
            continue;
        }
        if (PacketType != PI_HCI_PACKET_EVENT) {
            continue;
        }
        if (PiUartReadByte(&EventCode, 20u) != 0 ||
            PiUartReadByte(&EventLength, 20u) != 0) {
            continue;
        }
        for (Index = 0u; Index < EventLength; Index++) {
            if (PiUartReadByte(&Event[Index], 20u) != 0) {
                EventLength = 0u;
                break;
            }
        }
        if (EventCode != PI_HCI_EVENT_COMMAND_COMPLETE || EventLength < 4u) {
            continue;
        }
        /* CC: [num_cmds][opcode_lo][opcode_hi][status][return...] */
        if ((UINT16)(Event[1] | ((UINT16)Event[2] << 8)) != Opcode) {
            continue;
        }
        *OutStatus = Event[3];
        if (EventLength > 4u) {
            ReturnLength = (UINT8)(EventLength - 4u);
            if (OutReturn != 0 && ReturnMax > 0u) {
                if (ReturnLength > ReturnMax) {
                    ReturnLength = ReturnMax;
                }
                for (Index = 0u; Index < ReturnLength; Index++) {
                    OutReturn[Index] = Event[4u + Index];
                }
            }
        }
        if (OutReturnLength != 0) {
            *OutReturnLength = ReturnLength;
        }
        return 0;
    }
}

int PiHciSendCommand(UINT16 Opcode, const UINT8 *Parameters, UINT8 ParameterLength,
                     UINT8 *OutStatus, UINT8 *OutReturn, UINT8 ReturnMax,
                     UINT8 *OutReturnLength) {
    return PiHciSendCommandWithTimeout(Opcode, Parameters, ParameterLength, OutStatus,
                                       OutReturn, ReturnMax, OutReturnLength,
                                       PI_BT_RESPONSE_TIMEOUT_MS);
}
