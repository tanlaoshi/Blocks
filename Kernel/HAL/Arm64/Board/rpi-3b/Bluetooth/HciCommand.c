/*
 * HciCommand.c — HCI 命令收发（H4 之上的最小子集）
 *
 * 只做 bt-pi-1 需要的：发一条命令，等 Command Complete(0x0E)，取 status。
 * 参考：Core Spec Vol 4 Part E（HCI 命令流）+ 本仓 NUC Hci.c 的判据语义
 *       （HCI_Reset 的 CC 里 status=0 才算成功）。
 *
 * 别改：收包超时返回错误码，不吞不猜；只认 Command Complete，不把 Command Status 当成功。
 */
#include "BluetoothPrivate.h"
#include "Hal.h"

int PiHciSendCommand(UINT16 Opcode, const UINT8 *Parameters, UINT8 ParameterLength,
                     UINT8 *OutStatus) {
    UINT8 Header[4];
    UINT32 Index;
    UINT32 Start;
    UINT8 PacketType = 0u;
    UINT8 EventCode = 0u;
    UINT8 EventLength = 0u;
    UINT8 Event[PI_HCI_EVENT_MAX];

    if (OutStatus == 0) {
        return -1;
    }
    *OutStatus = 0xFFu;
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

    /* 等一个事件包：0x04 | event_code | plen | payload */
    Start = HalMilliseconds();
    for (;;) {
        if ((UINT32)(HalMilliseconds() - Start) >= PI_BT_RESPONSE_TIMEOUT_MS) {
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
        /* EventLength 是 u8（≤255），Event[] 有 PI_HCI_EVENT_MAX(260) 字节，不会溢出 */
        for (Index = 0u; Index < EventLength; Index++) {
            if (PiUartReadByte(&Event[Index], 20u) != 0) {
                EventLength = 0u;
                break;
            }
        }
        if (EventCode != PI_HCI_EVENT_COMMAND_COMPLETE || EventLength < 4u) {
            continue;
        }
        /* CC: [num_cmds][opcode_lo][opcode_hi][status...] */
        if ((UINT16)(Event[1] | ((UINT16)Event[2] << 8)) != Opcode) {
            continue;
        }
        *OutStatus = Event[3];
        return 0;
    }
}
