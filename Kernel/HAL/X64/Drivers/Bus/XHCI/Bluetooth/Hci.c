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

    BluetoothHciArmEvent();
    /*
     * Intel bootloader：0xFC09 必须 Bulk OUT（control 会送出但无 CC）。
     * 0xFC01 Intel Reset：设备不回 CC，调用方勿走本函数等 CC。
     */
    if (Opcode == HCI_OPCODE_INTEL_SECURE_SEND) {
        if (BluetoothHciSendBulk(Opcode, Params, ParamLen) < 0) {
            BootLog("Boot: Bluetooth Intel FC09 bulk fail\n");
            BluetoothRequeueHid();
            return -1;
        }
    } else if (BluetoothHciSendRaw(Opcode, Params, ParamLen) < 0) {
        BluetoothRequeueHid();
        return -1;
    }
    if (BluetoothHciWaitArmed(WaitMs) < 0) {
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

int BluetoothHciCommand(UINT16 Opcode, const UINT8 *Params, UINT8 ParamLen,
                        UINT8 *StatusOut, UINT8 *ReturnData, UINT8 ReturnMax,
                        UINT8 *ReturnLenOut) {
    return BluetoothHciCommandTimeout(Opcode, Params, ParamLen, StatusOut,
                                      ReturnData, ReturnMax, ReturnLenOut,
                                      2000u);
}

/*
 * 8087:0A2B = Linux BTUSB_INTEL_NEW。冷启多在 bootloader：
 * 标准 HCI_Reset → CC status=0x01；厂商 0xFC05 仍可用。
 */
static void BluetoothProbeIntelVersion(void) {
    UINT8 Status = 0xFFu;
    UINT8 Ver[12];
    UINT8 RetLen = 0;
    UINT32 I;

    ZeroMemory(Ver, sizeof(Ver));
    if (BluetoothHciCommand(HCI_OPCODE_INTEL_VERSION, 0, 0, &Status, Ver,
                            sizeof(Ver), &RetLen) < 0) {
        BootLog("Boot: Bluetooth Intel ver send/event fail\n");
        return;
    }
    BootLogHex("Boot: Bluetooth Intel ver status=", Status, 2);
    BootLogHex(" retlen=", RetLen, 2);
    for (I = 0; I < RetLen && I < sizeof(Ver); I++) {
        BootLogHex(" v=", Ver[I], 2);
    }
    if (Status == 0 && RetLen >= 4u) {
        BootLogHex("Boot: Bluetooth Intel fw_variant=", Ver[3], 2);
        if (Ver[3] == 0x06u) {
            BootLog("Boot: Bluetooth Intel mode=bootloader (need fw)\n");
        } else if (Ver[3] == 0x23u) {
            BootLog("Boot: Bluetooth Intel mode=operational\n");
        } else {
            BootLog("Boot: Bluetooth Intel mode=other\n");
        }
    }
}

int BluetoothHciReset(void) {
    UINT8 Status = 0xFF;
    int Attempt;

    gBluetoothHciResetOk = 0;
    gBluetoothReady = 0;
    for (Attempt = 0; Attempt < 2; Attempt++) {
        Status = 0xFF;
        if (BluetoothHciCommand(HCI_OPCODE_RESET, 0, 0, &Status, 0, 0, 0) < 0) {
            BootLog("Boot: Bluetooth HCI Reset send/event fail\n");
        } else {
            BootLogHex("Boot: Bluetooth CC opcode=", HCI_OPCODE_RESET, 4);
            BootLogHex(" status=", Status, 2);
            if (Status == 0) {
                gBluetoothHciResetOk = 1;
                BootLog("Boot: Bluetooth HCI Reset ok\n");
                return 1;
            }
            BootLog("Boot: Bluetooth HCI Reset bad status\n");
        }
        if (Attempt == 0) {
            if (!BluetoothIntelFirmwareLoad()) {
                BluetoothProbeIntelVersion();
                return 0;
            }
        }
    }
    BluetoothProbeIntelVersion();
    return 0;
}
