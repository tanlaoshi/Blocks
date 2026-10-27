/*
 * Hci.c — HCI_Reset + 事件收（btx-2）
 *
 * 命令：USB Class Control（bmRequestType=0x20）到 HCI 接口。
 * 事件：先投 Interrupt IN，再发命令，免得丢 Command Complete。
 * 等待窗内 ProcessEvents + ServiceHidCompletions（鼠优先）。
 */
#include "XHCI/Bluetooth/Private.h"

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
    ZeroMemory(gBluetoothEventBuffer, sizeof(gBluetoothEventBuffer));
    FlushDma(gBluetoothEventBuffer, sizeof(gBluetoothEventBuffer));
    gBluetoothEventDone = 0;
    gBluetoothEventCode = 0;
    InitRing(gBluetoothEventRing, &gBluetoothEvent, RING_SIZE);
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

static UINT8 gBluetoothCommandBuffer[16] BLOCKS_ALIGNED(64);

static int SendResetCommand(void) {
    USB_SETUP_PACKET Setup;

    gBluetoothCommandBuffer[0] = (UINT8)(HCI_OPCODE_RESET & 0xFFu);
    gBluetoothCommandBuffer[1] = (UINT8)((HCI_OPCODE_RESET >> 8) & 0xFFu);
    gBluetoothCommandBuffer[2] = 0;
    Setup.bmRequestType = 0x20;
    Setup.bRequest = 0x00;
    Setup.wValue = 0;
    Setup.wIndex = gBluetoothInterface;
    Setup.wLength = 3;
    FlushDma(gBluetoothCommandBuffer, 3);
    gXferSlot = gBluetoothSlot;
    return ControlXfer(&Setup, gBluetoothCommandBuffer);
}

static int EventIsResetComplete(void) {
    UINT8 EventCode;
    UINT8 ParameterLength;
    UINT16 Opcode;
    UINT8 Status;

    EventCode = gBluetoothEventBuffer[0];
    ParameterLength = gBluetoothEventBuffer[1];
    if (EventCode != HCI_EVENT_COMMAND_COMPLETE) {
        BootLogHex("Boot: Bluetooth event code=", EventCode, 2);
        return 0;
    }
    if (ParameterLength < 3u) {
        BootLog("Boot: Bluetooth event short\n");
        return 0;
    }
    Opcode = (UINT16)(gBluetoothEventBuffer[3] |
                      (gBluetoothEventBuffer[4] << 8));
    Status = gBluetoothEventBuffer[5];
    BootLogHex("Boot: Bluetooth CC opcode=", Opcode, 4);
    BootLogHex(" status=", Status, 2);
    return (Opcode == HCI_OPCODE_RESET && Status == 0) ? 1 : 0;
}

int BluetoothHciReset(void) {
    if (!gBluetoothConfigured || gBluetoothSlot == 0 ||
        gBluetoothEventDci == 0) {
        return 0;
    }
    gBluetoothHciResetOk = 0;
    QueueEventReceive();
    if (SendResetCommand() < 0) {
        BootLog("Boot: Bluetooth HCI Reset send fail\n");
        BluetoothRequeueHid();
        return 0;
    }
    if (WaitEvent(2000u) < 0) {
        BootLogHex("Boot: Bluetooth HCI Reset event fail cc=",
                   gBluetoothEventCode, 2);
        BluetoothRequeueHid();
        return 0;
    }
    if (!EventIsResetComplete()) {
        BootLog("Boot: Bluetooth HCI Reset bad event\n");
        BluetoothRequeueHid();
        return 0;
    }
    gBluetoothHciResetOk = 1;
    BootLog("Boot: Bluetooth HCI Reset ok\n");
    BluetoothRequeueHid();
    return 1;
}
