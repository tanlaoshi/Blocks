/*
 * HciSend.c — HCI 命令发送：Control（常态）/ Bulk OUT（Intel 0xFC09）
 *
 * bootloader 0xFC09：Bulk OUT 发命令，Bulk IN 收事件（对表 btusb）。
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
        BootLogHex("Boot: Bluetooth HCI control fail op=", Opcode, 4);
        return -1;
    }
    return 0;
}

void BluetoothHciArmAclIn(void) {
    UINT32 Len = gBluetoothAclInMaxPacketSize ? gBluetoothAclInMaxPacketSize
                                              : 64u;

    /* 泵常驻 ACL IN；bootloader FC09 仅在 Ready/泵前走本路径 */
    if (BluetoothHostPumpIsActive()) {
        return;
    }
    if (!gBluetoothConfigured || gBluetoothAclInDci == 0) {
        return;
    }
    if (Len > BLUETOOTH_EVENT_MAX) {
        Len = BLUETOOTH_EVENT_MAX;
    }
    ZeroMemory(gBluetoothEventBuffer, sizeof(gBluetoothEventBuffer));
    FlushDma(gBluetoothEventBuffer, sizeof(gBluetoothEventBuffer));
    gBluetoothAclInDone = 0;
    gBluetoothAclInCode = 0;
    gBluetoothAclInRemain = 0;
    gBluetoothEventDone = 0;
    gBluetoothEventCode = 0;
    Enqueue(gBluetoothAclInRing, &gBluetoothAclIn,
            PointerToPhysical(gBluetoothEventBuffer), Len,
            TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
    FlushDma(gBluetoothAclInRing, sizeof(gBluetoothAclInRing));
    RingDoorbell(gBluetoothSlot, gBluetoothAclInDci);
}

int BluetoothHciWaitAclIn(UINT32 TimeoutMs) {
    UINT64 Start = ReadTsc();
    UINT64 Need = (UINT64)TimeoutMs * 3000000ULL;
    int Own = 0;
    int Result = -1;

    if (!EventIsExclusive()) {
        EventEnterExclusive();
        Own = 1;
    }
    for (;;) {
        if (HalCpuIsHypervisor()) {
            ProcessEvents();
        } else {
            ProcessEventsRealPc();
        }
        ServiceHidCompletions();
        if (gBluetoothAclInDone) {
            Result = (gBluetoothAclInCode == CC_SUCCESS ||
                      gBluetoothAclInCode == CC_SHORT_PACKET)
                         ? 0
                         : -1;
            gBluetoothEventDone = 1;
            gBluetoothEventCode = gBluetoothAclInCode;
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
    FlushDma(gBluetoothEventBuffer, sizeof(gBluetoothEventBuffer));
    return Result;
}

static void BluetoothHciEnqueueBulkOut(UINT64 Physical, UINT32 Total) {
    UINT32 MaxPacketSize =
        gBluetoothAclOutMaxPacketSize ? gBluetoothAclOutMaxPacketSize : 64u;
    UINT32 Offset = 0;

    if (MaxPacketSize == 0u) {
        MaxPacketSize = 64u;
    }
    while (Offset < Total) {
        UINT32 Take = Total - Offset;
        UINT32 Control;

        if (Take > MaxPacketSize) {
            Take = MaxPacketSize;
        }
        if (Offset + Take < Total) {
            Control = TRB_TYPE(TRB_NORMAL) | TRB_CHAIN;
        } else {
            Control = TRB_TYPE(TRB_NORMAL) | TRB_IOC;
        }
        Enqueue(gBluetoothAclOutRing, &gBluetoothAclOut, Physical + Offset, Take,
                Control);
        Offset += Take;
    }
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
    BluetoothHciEnqueueBulkOut(PointerToPhysical(gBluetoothCommandBuffer),
                               Total);
    FlushDma(gBluetoothAclOutRing, sizeof(gBluetoothAclOutRing));
    RingDoorbell(gBluetoothSlot, gBluetoothAclOutDci);
    Start = ReadTsc();
    Need = 5000ULL * 3000000ULL;
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
