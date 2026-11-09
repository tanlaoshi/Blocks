/*
 * HciSend.c — HCI 命令发送：Control（常态）/ Bulk OUT（Intel 0xFC09）
 */
#include "XHCI/Bluetooth/Private.h"

static UINT8 gBluetoothCommandBuffer[BLUETOOTH_COMMAND_MAX] BLOCKS_ALIGNED(64);

static int BluetoothHciFillCommand(UINT16 Opcode, const UINT8 *Params,
                                   UINT8 ParamLen, UINT32 *TotalOut) {
    UINT32 Total;
    UINT8 I;

    Total = 3u + (UINT32)ParamLen;
    if (Total > BLUETOOTH_COMMAND_MAX) {
        return -1;
    }
    gBluetoothCommandBuffer[0] = (UINT8)(Opcode & 0xFFu);
    gBluetoothCommandBuffer[1] = (UINT8)((Opcode >> 8) & 0xFFu);
    gBluetoothCommandBuffer[2] = ParamLen;
    for (I = 0; I < ParamLen; I++) {
        gBluetoothCommandBuffer[3u + I] = Params ? Params[I] : 0;
    }
    *TotalOut = Total;
    return 0;
}

int BluetoothHciSendRaw(UINT16 Opcode, const UINT8 *Params, UINT8 ParamLen) {
    USB_SETUP_PACKET Setup;
    UINT32 Total;

    if (!gBluetoothConfigured || gBluetoothSlot == 0 ||
        gBluetoothEventDci == 0) {
        return -1;
    }
    if (BluetoothHciFillCommand(Opcode, Params, ParamLen, &Total) < 0) {
        return -1;
    }
    Setup.bmRequestType = 0x20;
    Setup.bRequest = 0x00;
    Setup.wValue = 0;
    Setup.wIndex = gBluetoothInterface;
    Setup.wLength = (UINT16)Total;
    FlushDma(gBluetoothCommandBuffer, Total);
    gTransferSlot = gBluetoothSlot;
    if (ControlTransfer(&Setup, gBluetoothCommandBuffer) < 0) {
        return -1;
    }
    return 0;
}

int BluetoothHciSendBulk(UINT16 Opcode, const UINT8 *Params, UINT8 ParamLen) {
    UINT32 Total;
    UINT64 Start;
    UINT64 Need;
    int Own = 0;
    int Result = -1;

    if (!gBluetoothConfigured || gBluetoothSlot == 0 ||
        gBluetoothAclOutDci == 0) {
        return -1;
    }
    if (BluetoothHciFillCommand(Opcode, Params, ParamLen, &Total) < 0) {
        return -1;
    }
    FlushDma(gBluetoothCommandBuffer, Total);
    gBluetoothAclOutDone = 0;
    gBluetoothAclOutCode = 0;
    if (!EventIsExclusive()) {
        EventEnterExclusive();
        Own = 1;
    }
    Enqueue(gBluetoothAclOutRing, &gBluetoothAclOut,
            PointerToPhysical(gBluetoothCommandBuffer), Total,
            TRB_TYPE(TRB_NORMAL) | TRB_IOC);
    FlushDma(gBluetoothAclOutRing, sizeof(gBluetoothAclOutRing));
    RingDoorbell(gBluetoothSlot, gBluetoothAclOutDci);
    Start = ReadTsc();
    Need = 2000ULL * 3000000ULL;
    for (;;) {
        if (HalCpuIsHypervisor()) {
            ProcessEvents();
        } else {
            ProcessEventsRealPc();
        }
        ServiceHidCompletions();
        if (gBluetoothAclOutDone) {
            Result = (gBluetoothAclOutCode == CC_SUCCESS ||
                      gBluetoothAclOutCode == CC_SHORT_PACKET)
                         ? 0
                         : -1;
            break;
        }
        if (ReadTsc() - Start >= Need) {
            break;
        }
        __asm__ volatile("pause");
    }
    if (Own) {
        EventLeaveExclusive();
    }
    return Result;
}
