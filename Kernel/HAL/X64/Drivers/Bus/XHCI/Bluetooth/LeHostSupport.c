/*
 * LeHostSupport.c — LE 主机侧开关（PR-BTM-3）
 *
 * 规格：Set Event Mask 开 LE Meta；旧双模控还要 Write LE Host Supported，
 * 否则 LE Set Scan Parameters/Enable 常 status=0x0C Command Disallowed。
 */
#include "XHCI/Bluetooth/Private.h"

static UINT16 sLeLastOpcode;
static UINT8 sLeLastStatus;

void BluetoothLeLastErrorSet(UINT16 Opcode, UINT8 Status) {
    sLeLastOpcode = Opcode;
    sLeLastStatus = Status;
}

void BluetoothLeLastErrorGet(UINT16 *OpcodeOut, UINT8 *StatusOut) {
    if (OpcodeOut != 0) {
        *OpcodeOut = sLeLastOpcode;
    }
    if (StatusOut != 0) {
        *StatusOut = sLeLastStatus;
    }
}

void XhciBluetoothLeLastError(UINT16 *OpcodeOut, UINT8 *StatusOut) {
    BluetoothLeLastErrorGet(OpcodeOut, StatusOut);
}

static int LeHostCommand(UINT16 Opcode, const UINT8 *Params, UINT8 ParamLen,
                         int SoftFail) {
    UINT8 Status = 0xFFu;

    /* FE=等 CC 超时：不可 SoftFail 吞掉，否则后续 200B 连环挂 */
    if (BluetoothHciCommand(Opcode, Params, ParamLen, &Status, 0, 0, 0) < 0) {
        BluetoothLeLastErrorSet(Opcode, 0xFEu);
        BootLogHex("bt: le-host cmd fail op=", Opcode, 4);
        return -1;
    }
    if (Status != 0) {
        BluetoothLeLastErrorSet(Opcode, Status);
        BootLogHex("bt: le-host status=", Status, 2);
        BootLogHex(" op=", Opcode, 4);
        /* SoftFail 只放过 Unknown/Disallowed 等状态字，不放过超时 */
        if (SoftFail) {
            return 0;
        }
        return -1;
    }
    return 0;
}

int BluetoothLeHostSupportApply(void) {
    UINT8 EventMask[8];
    UINT8 LeMask[8];
    UINT8 HostSupported[2];
    UINT32 Index;

    /*
     * bit61 = LE Meta Event → Mask[7] bit5。
     * 其余按课堂常用全开；控拒绝个别位时 SoftFail 继续。
     */
    for (Index = 0; Index < 8u; Index++) {
        EventMask[Index] = 0xFFu;
    }
    EventMask[7] = 0x3Fu;
    if (LeHostCommand(HCI_OPCODE_SET_EVENT_MASK, EventMask, 8, 1) < 0) {
        return -1;
    }

    HostSupported[0] = 1u; /* LE_Supported_Host */
    HostSupported[1] = 0u; /* Simultaneous_LE_Host */
    if (LeHostCommand(HCI_OPCODE_WRITE_LE_HOST_SUPPORTED, HostSupported, 2,
                      1) < 0) {
        return -1;
    }

    for (Index = 0; Index < 8u; Index++) {
        LeMask[Index] = 0;
    }
    LeMask[0] = 0x1Fu; /* Adv Report 等 */
    /* SoftFail：部分固件拒重复 Set；真正开扫由 Set Scan Parameters 判 */
    if (LeHostCommand(HCI_OPCODE_LE_SET_EVENT_MASK, LeMask, 8, 1) < 0) {
        return -1;
    }
    return 0;
}
