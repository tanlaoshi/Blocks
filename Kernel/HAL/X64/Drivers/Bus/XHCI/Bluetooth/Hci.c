/*
 * Hci.c — HCI 命令 / 事件（btx-2/3）
 *
 * Class Control 发命令；先投 Interrupt IN 再发，收 Command Complete。
 * 等待窗 ProcessEvents + ServiceHidCompletions（鼠优先）。
 */
#include "XHCI/Bluetooth/Private.h"

static UINT8 gBluetoothCommandBuffer[BLUETOOTH_COMMAND_MAX] BLOCKS_ALIGNED(64);

int BluetoothMatchTransfer(UINT32 EventSlot, UINT32 Endpoint, UINT64 TransferRing,
                           UINT32 Code, UINT32 Remain) {
    UINT64 Lo;
    UINT64 Hi;

    (void)Remain;
    if (!gBluetoothConfigured || gBluetoothSlot == 0 ||
        EventSlot != gBluetoothSlot) {
        return 0;
    }
    Lo = PointerToPhysical(gBluetoothEventRing);
    Hi = Lo + sizeof(gBluetoothEventRing);
    if ((gBluetoothEventDci != 0 && Endpoint == gBluetoothEventDci) ||
        (TransferRing >= Lo && TransferRing < Hi)) {
        gBluetoothEventCode = Code;
        gBluetoothEventDone = 1;
        return 1;
    }
    return 0;
}

static void QueueEventReceive(void) {
    UINT32 Len = gBluetoothEventMps ? gBluetoothEventMps : 16u;

    if (Len > BLUETOOTH_EVENT_MAX) {
        Len = BLUETOOTH_EVENT_MAX;
    }
    /*
     * 勿每次 InitRing：ConfigEP 已钉 dequeue；Reset 后再 Init 会与 HC
     * 失步 → 后续 Read_BD_ADDR 等不到事件 → Ready=0（对表 HID QueueIntr）。
     */
    ZeroMemory(gBluetoothEventBuffer, sizeof(gBluetoothEventBuffer));
    FlushDma(gBluetoothEventBuffer, sizeof(gBluetoothEventBuffer));
    gBluetoothEventDone = 0;
    gBluetoothEventCode = 0;
    Enqueue(gBluetoothEventRing, &gBluetoothEvent,
            PointerToPhysical(gBluetoothEventBuffer), Len,
            TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
    FlushDma(gBluetoothEventRing, sizeof(gBluetoothEventRing));
    RingDoorbell(gBluetoothSlot, gBluetoothEventDci);
}

static int WaitEvent(UINT32 TimeoutMs) {
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
        if (gBluetoothEventDone) {
            Result = (gBluetoothEventCode == CC_SUCCESS ||
                      gBluetoothEventCode == CC_SHORT_PACKET)
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
    FlushDma(gBluetoothEventBuffer, sizeof(gBluetoothEventBuffer));
    return Result;
}

int BluetoothHciCommand(UINT16 Opcode, const UINT8 *Params, UINT8 ParamLen,
                        UINT8 *StatusOut, UINT8 *ReturnData, UINT8 ReturnMax,
                        UINT8 *ReturnLenOut) {
    USB_SETUP_PACKET Setup;
    UINT32 Total;
    UINT8 I;
    UINT16 GotOpcode;
    UINT8 ParameterLength;
    UINT8 Status;
    UINT8 RetLen;

    if (!gBluetoothConfigured || gBluetoothSlot == 0 ||
        gBluetoothEventDci == 0) {
        return -1;
    }
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
    QueueEventReceive();
    Setup.bmRequestType = 0x20;
    Setup.bRequest = 0x00;
    Setup.wValue = 0;
    Setup.wIndex = gBluetoothInterface;
    Setup.wLength = (UINT16)Total;
    FlushDma(gBluetoothCommandBuffer, Total);
    gXferSlot = gBluetoothSlot;
    if (ControlXfer(&Setup, gBluetoothCommandBuffer) < 0) {
        BluetoothRequeueHid();
        return -1;
    }
    if (WaitEvent(2000u) < 0) {
        BootLogHex("Boot: Bluetooth HCI cmd timeout op=", Opcode, 4);
        BootLogHex(" xfer-cc=", gBluetoothEventCode, 2);
        BluetoothRequeueHid();
        return -1;
    }
    if (gBluetoothEventBuffer[0] != HCI_EVENT_COMMAND_COMPLETE) {
        BootLogHex("Boot: Bluetooth event code=", gBluetoothEventBuffer[0], 2);
        BootLogHex(" want-op=", Opcode, 4);
        BluetoothRequeueHid();
        return -1;
    }
    ParameterLength = gBluetoothEventBuffer[1];
    if (ParameterLength < 3u) {
        BluetoothRequeueHid();
        return -1;
    }
    GotOpcode = (UINT16)(gBluetoothEventBuffer[3] |
                         (gBluetoothEventBuffer[4] << 8));
    if (GotOpcode != Opcode) {
        BootLogHex("Boot: Bluetooth CC opcode=", GotOpcode, 4);
        BluetoothRequeueHid();
        return -1;
    }
    Status = 0;
    RetLen = 0;
    if (ParameterLength >= 4u) {
        Status = gBluetoothEventBuffer[5];
        RetLen = (UINT8)(ParameterLength - 4u);
        if (ReturnData != 0 && ReturnMax > 0 && RetLen > 0) {
            if (RetLen > ReturnMax) {
                RetLen = ReturnMax;
            }
            for (I = 0; I < RetLen; I++) {
                ReturnData[I] = gBluetoothEventBuffer[6u + I];
            }
        }
    }
    if (StatusOut) {
        *StatusOut = Status;
    }
    if (ReturnLenOut) {
        *ReturnLenOut = RetLen;
    }
    BluetoothRequeueHid();
    return 0;
}

int BluetoothHciReset(void) {
    UINT8 Status = 0xFF;

    gBluetoothHciResetOk = 0;
    gBluetoothReady = 0;
    if (BluetoothHciCommand(HCI_OPCODE_RESET, 0, 0, &Status, 0, 0, 0) < 0) {
        BootLog("Boot: Bluetooth HCI Reset send/event fail\n");
        return 0;
    }
    BootLogHex("Boot: Bluetooth CC opcode=", HCI_OPCODE_RESET, 4);
    BootLogHex(" status=", Status, 2);
    if (Status != 0) {
        BootLog("Boot: Bluetooth HCI Reset bad status\n");
        return 0;
    }
    gBluetoothHciResetOk = 1;
    BootLog("Boot: Bluetooth HCI Reset ok\n");
    return 1;
}
