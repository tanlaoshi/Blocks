/*
 * Hci.c — HCI 事件臂/等 + 命令完整事务（btx-2…4）
 *
 * 发送见 HciSend.c。Intel bootloader 0xFC09 走 Bulk（对表 btusb）。
 * 等待窗 ProcessEvents + ServiceHidCompletions（鼠优先）。
 */
#include "XHCI/Bluetooth/Private.h"

int BluetoothMatchTransfer(UINT32 EventSlot, UINT32 Endpoint, UINT64 TransferRing,
                           UINT32 Code, UINT32 Remain) {
    UINT64 EventLo;
    UINT64 EventHi;
    UINT64 AclInLo;
    UINT64 AclInHi;
    UINT64 AclOutLo;
    UINT64 AclOutHi;

    (void)Remain;
    if (!gBluetoothConfigured || gBluetoothSlot == 0 ||
        EventSlot != gBluetoothSlot) {
        return 0;
    }
    EventLo = PointerToPhysical(gBluetoothEventRing);
    EventHi = EventLo + sizeof(gBluetoothEventRing);
    if ((gBluetoothEventDci != 0 && Endpoint == gBluetoothEventDci) ||
        (TransferRing >= EventLo && TransferRing < EventHi)) {
        gBluetoothEventCode = Code;
        gBluetoothEventDone = 1;
        return 1;
    }
    AclInLo = PointerToPhysical(gBluetoothAclInRing);
    AclInHi = AclInLo + sizeof(gBluetoothAclInRing);
    if ((gBluetoothAclInDci != 0 && Endpoint == gBluetoothAclInDci) ||
        (TransferRing >= AclInLo && TransferRing < AclInHi)) {
        gBluetoothAclInCode = Code;
        gBluetoothAclInDone = 1;
        return 1;
    }
    AclOutLo = PointerToPhysical(gBluetoothAclOutRing);
    AclOutHi = AclOutLo + sizeof(gBluetoothAclOutRing);
    if ((gBluetoothAclOutDci != 0 && Endpoint == gBluetoothAclOutDci) ||
        (TransferRing >= AclOutLo && TransferRing < AclOutHi)) {
        gBluetoothAclOutCode = Code;
        gBluetoothAclOutDone = 1;
        return 1;
    }
    return 0;
}

void BluetoothHciArmEvent(void) {
    UINT32 Len = gBluetoothEventMaxPacketSize ? gBluetoothEventMaxPacketSize : 16u;

    if (Len > BLUETOOTH_EVENT_MAX) {
        Len = BLUETOOTH_EVENT_MAX;
    }
    /*
     * 勿每次 RingInitialize：ConfigEP 已钉 dequeue；Reset 后再 Init 会与 HC
     * 失步 → 后续命令等不到事件 → Ready=0（对表 HID QueueIntr）。
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

int BluetoothHciWaitArmed(UINT32 TimeoutMs) {
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

int BluetoothHciCommandTimeout(UINT16 Opcode, const UINT8 *Params, UINT8 ParamLen,
                               UINT8 *StatusOut, UINT8 *ReturnData,
                               UINT8 ReturnMax, UINT8 *ReturnLenOut,
                               UINT32 TimeoutMilliseconds) {
    UINT8 I;
    UINT16 GotOpcode;
    UINT8 ParameterLength;
    UINT8 Status;
    UINT8 RetLen;
    UINT32 WaitMs = TimeoutMilliseconds ? TimeoutMilliseconds : 2000u;

    /*
     * Intel bootloader 0xFC09：Bulk OUT 发命令、Bulk IN 收事件（对表 btusb）。
     * 勿 Arm Interrupt 等 CC——设备不往 0x81 吐，只会 deq==arm / xfer-cc=0。
     * 0xFC01 Intel Reset：设备不回 CC，调用方勿走本函数等 CC。
     */
    if (Opcode == HCI_OPCODE_INTEL_SECURE_SEND) {
        BluetoothHciArmAclIn();
        if (BluetoothHciSendBulk(Opcode, Params, ParamLen) < 0) {
            BootLog("Boot: Bluetooth Intel FC09 bulk fail\n");
            BluetoothRequeueHid();
            return -1;
        }
        if (BluetoothHciWaitAclIn(WaitMs) < 0) {
            BootLogHex("Boot: Bluetooth HCI cmd timeout op=", Opcode, 4);
            BootLogHex(" xfer-cc=", gBluetoothAclInCode, 2);
            BootLog(" (bulk-in)\n");
            BluetoothRequeueHid();
            return -1;
        }
    } else {
        BluetoothHciArmEvent();
        if (BluetoothHciSendRaw(Opcode, Params, ParamLen) < 0) {
            BluetoothRequeueHid();
            return -1;
        }
        if (BluetoothHciWaitArmed(WaitMs) < 0) {
            UINT32 *EventEndpointContext;
            UINT32 EndpointState = 0xFFu;
            UINT32 Unmatched = gStatUnmatched;
            UINT64 Dequeue = 0;
            UINT64 Armed = PointerToPhysical(gBluetoothEventRing);

            /*
             * 取证：Output Context Event EP。
             * st=1 Running + dequeue 仍停在武装 TRB + unmatched=0
             * → HC 在等设备；设备没往 Interrupt IN 吐 HCI 事件。
             */
            if (gBluetoothSlot != 0 && gBluetoothEventDci != 0 && gCtxSize != 0) {
                FlushDma(gBluetoothDeviceContext,
                         gCtxSize * (gBluetoothEventDci + 1u));
                EventEndpointContext =
                    (UINT32 *)(void *)(gBluetoothDeviceContext +
                                       gCtxSize * gBluetoothEventDci);
                EndpointState = EventEndpointContext[0] & 0x7u;
                Dequeue = (UINT64)EventEndpointContext[2] |
                          ((UINT64)EventEndpointContext[3] << 32);
                Dequeue &= ~0xFULL;
            }
            BootLogHex("Boot: Bluetooth HCI cmd timeout op=", Opcode, 4);
            BootLogHex(" xfer-cc=", gBluetoothEventCode, 2);
            BootLogHex(" ep-st=", EndpointState, 2);
            BootLogHex(" unmatched=", Unmatched, 4);
            BootLogHex(" mps=", gBluetoothEventMaxPacketSize, 4);
            BootLogHex(" deq=", (UINT32)Dequeue, 8);
            BootLogHex(" arm=", (UINT32)Armed, 8);
            BluetoothRequeueHid();
            return -1;
        }
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

int BluetoothHciCommand(UINT16 Opcode, const UINT8 *Params, UINT8 ParamLen,
                        UINT8 *StatusOut, UINT8 *ReturnData, UINT8 ReturnMax,
                        UINT8 *ReturnLenOut) {
    return BluetoothHciCommandTimeout(Opcode, Params, ParamLen, StatusOut,
                                      ReturnData, ReturnMax, ReturnLenOut,
                                      2000u);
}
