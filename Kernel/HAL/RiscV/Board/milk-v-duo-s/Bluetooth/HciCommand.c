/*
 * HciCommand.c — Duo H4 HCI 命令（对表 Pi HciCommand / NUC 判据）
 *
 * 只认 Command Complete(0x0E)；超时返回错误，不吞不猜。
 */
#include "BluetoothPrivate.h"
#include "Hal.h"

int DuoBluetoothHciSendCommand(UINT16 Opcode, const UINT8 *Parameters,
                               UINT8 ParameterLength, UINT8 *OutStatus,
                               UINT8 *OutReturn, UINT8 ReturnMax,
                               UINT8 *OutReturnLength) {
    UINT8 Header[4];
    UINT32 Index;
    UINT32 Start;
    UINT8 PacketType = 0u;
    UINT8 EventCode = 0u;
    UINT8 EventLength = 0u;
    UINT8 Event[DUO_HCI_EVENT_MAX];
    UINT8 ReturnLength = 0u;

    if (OutStatus == 0) {
        return -1;
    }
    *OutStatus = 0xFFu;
    if (OutReturnLength != 0) {
        *OutReturnLength = 0u;
    }
    Header[0] = DUO_HCI_PACKET_COMMAND;
    Header[1] = (UINT8)(Opcode & 0xFFu);
    Header[2] = (UINT8)(Opcode >> 8);
    Header[3] = ParameterLength;
    for (Index = 0u; Index < sizeof(Header); Index++) {
        DuoBluetoothUartWriteByte(Header[Index]);
    }
    for (Index = 0u; Index < ParameterLength; Index++) {
        DuoBluetoothUartWriteByte(Parameters[Index]);
    }

    Start = HalMilliseconds();
    for (;;) {
        if ((UINT32)(HalMilliseconds() - Start) >= DUO_BT_RESPONSE_TIMEOUT_MS) {
            return -2;
        }
        if (DuoBluetoothUartReadByte(&PacketType, 20u) != 0) {
            continue;
        }
        if (PacketType != DUO_HCI_PACKET_EVENT) {
            continue;
        }
        if (DuoBluetoothUartReadByte(&EventCode, 20u) != 0 ||
            DuoBluetoothUartReadByte(&EventLength, 20u) != 0) {
            continue;
        }
        for (Index = 0u; Index < EventLength; Index++) {
            if (DuoBluetoothUartReadByte(&Event[Index], 20u) != 0) {
                EventLength = 0u;
                break;
            }
        }
        if (EventCode != DUO_HCI_EVENT_COMMAND_COMPLETE || EventLength < 4u) {
            continue;
        }
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
