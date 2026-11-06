/*
 * HidInterruptKeyboard.c — PR-S-xhcihid-1：键盘中断端点配置 / 出队 / 入队
 * 从 Hid.c 原样搬家；不改语义。HID 全局仍在 Xhci.c。
 */
#include "XHCI/Private.h"

UINT8 FsInterval(UINT8 BInterval) {
    if (BInterval == 0) {
        BInterval = 1;
    }
    UINT8 Log2 = 0;
    UINT8 V = BInterval;
    while (V > 1) {
        V >>= 1;
        Log2++;
    }
    return (UINT8)(Log2 + 3);
}

/* 配置 HID 中断 IN 端点 */
/*
 * 首次 ConfigEP。MouseEndpointAddress!=0 时同一次 Add 键盘+鼠标（真机二次 ConfigEP 会弄死键盘，
 * 含 Add-only；NUC PHOTO：add-only ok 仍 k=0 m 正常）。
 */
int ConfigureInterrupt(UINT8 EndpointAddress, UINT16 MaxPacketSize, UINT8 BInterval, UINT8 Speed,
                         UINT8 MouseEndpointAddress, UINT16 MouseMaxPacketSize, UINT8 MouseBInterval) {
    UINT8 EndpointNum = EndpointAddress & 0x0F;
    UINT8 In = (EndpointAddress & 0x80) ? 1 : 0;
    UINT8 Interval;
    UINT32 CtxEntries;
    UINT32 AddFlags;

    gIntrDci = (UINT32)EndpointNum * 2 + In;
    gKeyboardEndpointAddress = EndpointAddress;
    if (MaxPacketSize == 0 || MaxPacketSize > 64) {
        MaxPacketSize = 8;
    }
    gKeyboardMaxPacketSize = MaxPacketSize;

    CtxEntries = gIntrDci;
    AddFlags = (1u << 0) | (1u << gIntrDci);
    if (MouseEndpointAddress != 0) {
        UINT8 MouseEndpointNum = MouseEndpointAddress & 0x0F;
        UINT8 MIn = (MouseEndpointAddress & 0x80) ? 1 : 0;

        gMouseIntrDci = (UINT32)MouseEndpointNum * 2 + MIn;
        gMouseEndpointAddress = MouseEndpointAddress;
        if (MouseMaxPacketSize == 0 || MouseMaxPacketSize > 64) {
            MouseMaxPacketSize = 8;
        }
        gMouseReportLen = (UINT8)(MouseMaxPacketSize > 8 ? 8 : MouseMaxPacketSize);
        if (gMouseReportLen < 3) {
            gMouseReportLen = 3;
        }
        if (gMouseIntrDci > CtxEntries) {
            CtxEntries = gMouseIntrDci;
        }
        AddFlags |= (1u << gMouseIntrDci);
    }

    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = AddFlags;

    UINT32 *Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | (gKeyboardRoute & 0xFFFFFu);
    Slot[1] = (UINT32)gPort1 << 16;
    if (gKeyboardHubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)gKeyboardHubSlot | ((UINT32)gKeyboardTransactionTranslatorPort << 8);
    }

    RingInitialize(gIntrRing, &gIntr, RING_SIZE);
    UINT32 *Endpoint = (UINT32 *)(void *)InEndpoint(gIntrDci);
    Interval = (Speed >= 3) ? (UINT8)((BInterval > 0) ? (BInterval - 1) : 0) : FsInterval(BInterval);
    gKeyboardEndpointInterval = Interval;
    Endpoint[0] = (UINT32)Interval << 16;
    Endpoint[1] = (3u << 1) | (7u << 3) | ((UINT32)MaxPacketSize << 16);
    UINT64 Deq = PointerToPhysical(gIntrRing) | 1;
    Endpoint[2] = (UINT32)Deq;
    Endpoint[3] = (UINT32)(Deq >> 32);
    /* Average TRB Length | Max ESIT Payload Lo（HID：=MPS；为 0 时部分 HC 不调度中断 IN） */
    Endpoint[4] = (UINT32)MaxPacketSize | ((UINT32)MaxPacketSize << 16);

    if (MouseEndpointAddress != 0) {
        UINT8 MInterval;
        UINT32 *MouseEndpoint;
        UINT64 MDeq;

        RingInitialize(gMouseIntrRing, &gMouseIntr, RING_SIZE);
        MouseEndpoint = (UINT32 *)(void *)InEndpoint(gMouseIntrDci);
        MInterval = (Speed >= 3)
                        ? (UINT8)((MouseBInterval > 0) ? (MouseBInterval - 1) : 0)
                        : FsInterval(MouseBInterval);
        MouseEndpoint[0] = (UINT32)MInterval << 16;
        MouseEndpoint[1] = (3u << 1) | (7u << 3) | ((UINT32)MouseMaxPacketSize << 16);
        MDeq = PointerToPhysical(gMouseIntrRing) | 1;
        MouseEndpoint[2] = (UINT32)MDeq;
        MouseEndpoint[3] = (UINT32)(MDeq >> 32);
        MouseEndpoint[4] = (UINT32)MouseMaxPacketSize | ((UINT32)MouseMaxPacketSize << 16);
        FlushDma(gMouseIntrRing, sizeof(gMouseIntrRing));
    }

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gIntrRing, sizeof(gIntrRing));

    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(gSlotId), 0) < 0) {
        DebugWrite("XHCI: Configure Endpoint failed\n");
        EnumerateWhy("Boot: Why=cfg ep\n");
        if (MouseEndpointAddress != 0) {
            gMouseIntrDci = 0;
            gMouseEndpointAddress = 0;
        }
        return 0;
    }
    if (MouseEndpointAddress != 0) {
        gMouseSlotId = gSlotId;
        ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
        BootLog("Boot: XHCI mouse with-kbd cfg ok\n");
    }
    {
        char Line[64];
        char Hex[12];
        int n = 0;
        const char *P = "Boot: XHCI kbd ep=";
        while (*P && n < 20) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, EndpointAddress, 2);
        P = Hex;
        while (*P && n < 28) {
            Line[n++] = *P++;
        }
        P = " dci=";
        while (*P && n < 36) {
            Line[n++] = *P++;
        }
        Line[n++] = (char)('0' + ((gIntrDci / 10) % 10));
        Line[n++] = (char)('0' + (gIntrDci % 10));
        Line[n++] = '\n';
        Line[n] = 0;
        BootLog(Line);
    }
    DebugWrite("XHCI: Interrupt EP configured\n");
    return 1;
}

/*
 * 真机 Arm：枚举期已挂中断 TRB。须先 Stop（环仍有效）→ 排空 Stopped 事件
 * → 再 RingInitialize → Set TR Dequeue；失败则 Reset EP 再试。
 * 旧序 RingInitialize 先于 Stop 会毁掉 HC 还在用的环，且 Stop 回调里 QueueIntr
 * 会导致 SetTrDeq 报 Context State Error (got=0x13)。
 */
int SyncIntrDequeue(UINT32 Slot, UINT32 Dci, XHCI_TRB *Ring, RING_STATE *St,
                           UINTN RingBytes) {
    UINT64 Deq;
    UINT32 EndpointField = (Dci & 0x1Fu) << 16;

    if (Slot == 0 || Dci == 0) {
        return -1;
    }

    /* 1) 先停 EP（此时环内容仍与硬件一致） */
    (void)Command(0, TRB_TYPE(TRB_STOP_EP) | TRB_SLOT(Slot) | EndpointField, 0);
    ProcessEvents();
    if (!HalCpuIsHypervisor()) {
        ProcessEventsRealPc();
    }

    /* 2) 软件环从头重建，再告诉 HC 新 dequeue */
    RingInitialize(Ring, St, RING_SIZE);
    FlushDma(Ring, RingBytes);
    Deq = PointerToPhysical(&Ring[St->Enq]) | (St->Pcs & 1u);
    if (Command(Deq, TRB_TYPE(TRB_SET_TR_DEQ) | TRB_SLOT(Slot) | EndpointField, 0) == 0) {
        return 0;
    }

    /* 3) Context State 等：Reset EP 后再 SetTrDeq */
    (void)Command(0, TRB_TYPE(TRB_RESET_EP) | TRB_SLOT(Slot) | EndpointField, 0);
    ProcessEvents();
    RingInitialize(Ring, St, RING_SIZE);
    FlushDma(Ring, RingBytes);
    Deq = PointerToPhysical(&Ring[St->Enq]) | (St->Pcs & 1u);
    if (Command(Deq, TRB_TYPE(TRB_SET_TR_DEQ) | TRB_SLOT(Slot) | EndpointField, 0) < 0) {
        LogUsb("Boot: XHCI sync deq fail\n");
        return -1;
    }
    return 0;
}

/* 复合鼠标配完后重排键盘中断。环留在本文件，调用方不传 TRB。 */
int SyncKeyboardIntr(void) {
    return SyncIntrDequeue(gSlotId, gIntrDci, gIntrRing, &gIntr, sizeof(gIntrRing));
}

/* 提交中断 IN：长度用首次 ConfigureInterrupt 的 MPS（勿超过 8） */
void QueueIntr(void) {
    UINT32 Len = gKeyboardMaxPacketSize;

    gIntrDone = 0;
    gIntrReportReady = 0;
    FlushDma(gReportBuf, sizeof(gReportBuf));
    if (Len == 0 || Len > 8) {
        Len = 8;
    }
    Enqueue(gIntrRing, &gIntr, PointerToPhysical(gReportBuf), Len,
            TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
    RingDoorbell(gSlotId, gIntrDci);
}

void KeyboardPush(void) {
    UINT32 Next;
    UINT8 *Dst;
    int i;

    /* 与 DequeueKeyboard 同一把锁。队满丢掉这份新报告，不盖住还没读的旧键。 */
    SpinLockAcquire(&gHidQueueLock);
    Next = (gKeyboardWriteIndex + 1) % KEYBOARD_QUEUE;
    if (Next == gKeyboardReadIndex) {
        SpinLockRelease(&gHidQueueLock);
        return;
    }
    Dst = (UINT8 *)&gKeyboardQueue[gKeyboardWriteIndex];
    for (i = 0; i < 8; i++) {
        Dst[i] = gReportBuf[i];
    }
    gKeyboardWriteIndex = Next;
    SpinLockRelease(&gHidQueueLock);
}

int DequeueKeyboard(USB_KEYBOARD_REPORT *Report) {
    int Ok = 0;

    SpinLockAcquire(&gHidQueueLock);
    if (gKeyboardReadIndex != gKeyboardWriteIndex) {
        UINT8 *Src = (UINT8 *)&gKeyboardQueue[gKeyboardReadIndex];
        UINT8 *Dst = (UINT8 *)Report;
        int i;

        for (i = 0; i < 8; i++) {
            Dst[i] = Src[i];
        }
        gKeyboardReadIndex = (gKeyboardReadIndex + 1) % KEYBOARD_QUEUE;
        Ok = 1;
    }
    SpinLockRelease(&gHidQueueLock);
    return Ok;
}
