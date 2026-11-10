/*
 * HidInterruptMouse.c — PR-S-xhcihid-1：鼠标中断端点配置 / 入队
 * 从 Hid.c 原样搬家；不改语义。HID 全局仍在 Xhci.c。
 * 复合鼠 ConfigEP 失败回退 → HidInterruptMouseFallback.c（PR-NORM-11）。
 */
#include "XHCI/Private.h"

int ConfigureMouseInterrupt(UINT32 SlotId, UINT8 EndpointAddress, UINT16 MaxPacketSize, UINT8 BInterval,
                              UINT8 Speed) {
    UINT8 EndpointNum = EndpointAddress & 0x0F;
    UINT8 In = (EndpointAddress & 0x80) ? 1 : 0;
    UINT32 CtxEntries;
    UINT32 Route = 0;
    UINT32 RootPort;
    UINT8 HubSlot = 0;
    UINT8 TtPort = 0;
    int Composite;
    int AddOnly;
    UINT8 Interval;
    UINT32 *Slot;
    UINT32 *Endpoint;
    UINT64 Deq;

    gMouseIntrDci = (UINT32)EndpointNum * 2 + In;
    gMouseEndpointAddress = EndpointAddress;
    if (MaxPacketSize == 0 || MaxPacketSize > 64) {
        MaxPacketSize = 8;
    }
    /* 与键盘一致：TRB 长度用 8+ISP；gMouseReportLen 仅作解析上限 */
    gMouseReportLen = (UINT8)(MaxPacketSize > 8 ? 8 : MaxPacketSize);
    if (gMouseReportLen < 3) {
        gMouseReportLen = 3;
    }

    CtxEntries = gMouseIntrDci;
    RootPort = gMousePort;
    Composite = (SlotId == gSlotId && gSlotId != 0);
    AddOnly = 0;

    /*
     * 复合设备：只 Add 鼠标，勿 Drop/重建键盘 EP（Linux：各 iface 独立）。
     * NUC PHOTO：Stop+Sync 后仍 k=0、m 正常 → 不要 Stop 键盘；
     * Running 时 Add 若 ConfigEP 失败再回退 Stop+Add-only / Drop+Add。
     */
    if (Composite) {
        if (gIntrDci > CtxEntries) {
            CtxEntries = gIntrDci;
        }
        Route = gKeyboardRoute & 0xFFFFFu;
        RootPort = gPort1;
        HubSlot = gKeyboardHubSlot;
        TtPort = gKeyboardTransactionTranslatorPort;
        Speed = gSpeed;
        AddOnly = 1;
    } else {
        /* 独立鼠标（含 hub 子口）：ConfigEP 必须带回 Address 时的 Route/TT */
        Route = gMouseRoute & 0xFFFFFu;
        HubSlot = gMouseHubSlot;
        TtPort = gMouseTtPort;
    }

    ZeroMemory(gInCtx, sizeof(gInCtx));
    if (Composite && gIntrDci != 0 && AddOnly) {
        /* 不 Drop 键盘；只 Add slot + 鼠标 DCI，抬高 Context Entries */
        *(UINT32 *)(void *)(gInCtx + 0) = 0;
        *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << gMouseIntrDci);
    } else if (Composite && gIntrDci != 0) {
        *(UINT32 *)(void *)(gInCtx + 0) = (1u << gIntrDci) | (1u << gMouseIntrDci);
        *(UINT32 *)(void *)(gInCtx + 4) =
            (1u << 0) | (1u << gIntrDci) | (1u << gMouseIntrDci);
    } else {
        *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << gMouseIntrDci);
    }

    Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | Route;
    Slot[1] = (UINT32)RootPort << 16;
    if (HubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)HubSlot | ((UINT32)TtPort << 8);
    }

    /* Drop+Add 回退路径才重填键盘 EP；Add-only 保持首次 ConfigureInterrupt 的上下文 */
    if (Composite && gIntrDci != 0 && !AddOnly) {
        UINT32 *KeyboardEndpoint = (UINT32 *)(void *)InEndpoint(gIntrDci);
        UINT8 KeyboardInterval;
        UINT16 KeyboardMaxPacketSize;
        UINT64 KeyboardDequeue;

        KeyboardMaxPacketSize = gKeyboardMaxPacketSize;
        if (KeyboardMaxPacketSize == 0 || KeyboardMaxPacketSize > 64) {
            KeyboardMaxPacketSize = 8;
        }
        KeyboardInterval = gKeyboardEndpointInterval;
        if (KeyboardInterval == 0) {
            KeyboardInterval = (Speed >= 3) ? 3 : FsInterval(10);
        }
        RingInitialize(gIntrRing, &gIntr, RING_SIZE);
        KeyboardEndpoint[0] = (UINT32)KeyboardInterval << 16;
        KeyboardEndpoint[1] = (3u << 1) | (7u << 3) | ((UINT32)KeyboardMaxPacketSize << 16);
        KeyboardDequeue = PointerToPhysical(gIntrRing) | 1;
        KeyboardEndpoint[2] = (UINT32)KeyboardDequeue;
        KeyboardEndpoint[3] = (UINT32)(KeyboardDequeue >> 32);
        KeyboardEndpoint[4] = (UINT32)KeyboardMaxPacketSize | ((UINT32)KeyboardMaxPacketSize << 16);
        FlushDma(gIntrRing, sizeof(gIntrRing));
    }

    RingInitialize(gMouseIntrRing, &gMouseIntr, RING_SIZE);
    Endpoint = (UINT32 *)(void *)InEndpoint(gMouseIntrDci);
    Interval = (Speed >= 3) ? (UINT8)((BInterval > 0) ? (BInterval - 1) : 0)
                            : FsInterval(BInterval);
    Endpoint[0] = (UINT32)Interval << 16;
    Endpoint[1] = (3u << 1) | (7u << 3) | ((UINT32)MaxPacketSize << 16);
    Deq = PointerToPhysical(gMouseIntrRing) | 1;
    Endpoint[2] = (UINT32)Deq;
    Endpoint[3] = (UINT32)(Deq >> 32);
    Endpoint[4] = (UINT32)MaxPacketSize | ((UINT32)MaxPacketSize << 16);

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gMouseIntrRing, sizeof(gMouseIntrRing));

    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) < 0) {
        if (Composite && AddOnly && gIntrDci != 0) {
            return ConfigureMouseInterruptFallback(SlotId, CtxEntries, Speed, Route,
                                                   RootPort, HubSlot, TtPort, Interval,
                                                   MaxPacketSize);
        }
        DebugWrite("XHCI: mouse endpoint failed\n");
        return 0;
    }
    if (Composite && AddOnly) {
        BootLog("Boot: XHCI mouse add-only ok\n");
        return 1; /* 键盘未 Stop：勿 Sync */
    }
    return 1;
}

void MouseHoldUntilReveal(int Hold) {
    if (Hold) {
        gMouseIntrHold = 1;
        gMouseIntrDone = 0;
        gMouseReportReady = 0;
        return;
    }
    gMouseIntrHold = 0;
    if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
        QueueMouseIntr();
    }
}

void QueueMouseIntr(void) {
    UINT32 Len;

    /* Hold 期间禁止再投递；Reveal 清 Hold 后再调本函数 */
    if (gMouseIntrHold) {
        return;
    }
    gMouseIntrDone = 0;
    gMouseReportReady = 0;
    /* 短包只写前 N 字节；不清零会让相对鼠被「高字节非0」启发式误判成绝对 */
    ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
    FlushDma(gMouseBuf, sizeof(gMouseBuf));
    /*
     * TRB 长度不得超过 EP MPS。真机 composite 鼠 mps=4 时曾固定 enqueue 8，
     * HC 不调度完成 → PHOTO m=0 而键盘正常。
     */
    Len = gMouseReportLen;
    if (Len == 0 || Len > 8) {
        Len = 8;
    }
    Enqueue(gMouseIntrRing, &gMouseIntr, PointerToPhysical(gMouseBuf), Len,
            TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
    RingDoorbell(gMouseSlotId, gMouseIntrDci);
}
