/*
 * HidInterruptMouseFallback.c — ConfigureMouseInterrupt 复合鼠失败回退（自 HidInterruptMouse.c 纯搬 · PR-NORM-11）
 *
 * Running 时 Add 失败：再试 Stop 后 Add-only；仍失败才 Drop+Add（保鼠标，键盘可能 k=0）。
 * 返回：0 失败；2 stop+add ok（调用方须 Sync+Queue）；3 drop-add（调用方 Sync 键盘）。
 */
#include "XHCI/Private.h"

int ConfigureMouseInterruptFallback(UINT32 SlotId, UINT32 CtxEntries, UINT8 Speed,
                                    UINT32 Route, UINT32 RootPort, UINT8 HubSlot,
                                    UINT8 TtPort, UINT8 Interval,
                                    UINT16 MaxPacketSize) {
    UINT32 EndpointField = (gIntrDci & 0x1Fu) << 16;
    UINT32 *Slot;
    UINT32 *Endpoint;
    UINT64 Deq;

    BootLog("Boot: XHCI mouse add-run fail, try stop+add\n");
    (void)Command(0, TRB_TYPE(TRB_STOP_EP) | TRB_SLOT(SlotId) | EndpointField, 0);
    ProcessEvents();
    if (!HalCpuIsHypervisor()) {
        ProcessEventsRealPc();
    }
    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 0) = 0;
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << gMouseIntrDci);
    Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | Route;
    Slot[1] = (UINT32)RootPort << 16;
    if (HubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)HubSlot | ((UINT32)TtPort << 8);
    }
    RingInitialize(gMouseIntrRing, &gMouseIntr, RING_SIZE);
    Endpoint = (UINT32 *)(void *)InEndpoint(gMouseIntrDci);
    Endpoint[0] = (UINT32)Interval << 16;
    Endpoint[1] = (3u << 1) | (7u << 3) | ((UINT32)MaxPacketSize << 16);
    Deq = PointerToPhysical(gMouseIntrRing) | 1;
    Endpoint[2] = (UINT32)Deq;
    Endpoint[3] = (UINT32)(Deq >> 32);
    Endpoint[4] = (UINT32)MaxPacketSize | ((UINT32)MaxPacketSize << 16);
    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gMouseIntrRing, sizeof(gMouseIntrRing));
    if (Command(PointerToPhysical(gInCtx),
                TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) == 0) {
        BootLog("Boot: XHCI mouse stop+add ok\n");
        /* 键盘曾 Stop：调用方须 Sync+Queue */
        return 2;
    }
    BootLog("Boot: XHCI mouse stop+add fail, drop-add\n");
    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 0) = (1u << gIntrDci) | (1u << gMouseIntrDci);
    *(UINT32 *)(void *)(gInCtx + 4) =
        (1u << 0) | (1u << gIntrDci) | (1u << gMouseIntrDci);
    Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | Route;
    Slot[1] = (UINT32)RootPort << 16;
    if (HubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)HubSlot | ((UINT32)TtPort << 8);
    }
    {
        UINT32 *KeyboardEndpoint = (UINT32 *)(void *)InEndpoint(gIntrDci);
        UINT8 KeyboardInterval = gKeyboardEndpointInterval ? gKeyboardEndpointInterval
                                     : (UINT8)((Speed >= 3) ? 3 : FsInterval(10));
        UINT16 KeyboardMaxPacketSize = (gKeyboardMaxPacketSize && gKeyboardMaxPacketSize <= 64) ? gKeyboardMaxPacketSize : 8;
        UINT64 KeyboardDequeue;

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
    Endpoint[0] = (UINT32)Interval << 16;
    Endpoint[1] = (3u << 1) | (7u << 3) | ((UINT32)MaxPacketSize << 16);
    Deq = PointerToPhysical(gMouseIntrRing) | 1;
    Endpoint[2] = (UINT32)Deq;
    Endpoint[3] = (UINT32)(Deq >> 32);
    Endpoint[4] = (UINT32)MaxPacketSize | ((UINT32)MaxPacketSize << 16);
    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gMouseIntrRing, sizeof(gMouseIntrRing));
    if (Command(PointerToPhysical(gInCtx),
                TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) < 0) {
        DebugWrite("XHCI: mouse endpoint failed\n");
        return 0;
    }
    return 3; /* drop-add：调用方 Sync 键盘 */
}
