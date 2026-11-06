/*
 * CdcTx.c — PR-H-usb-uart-cdc-1：CDC Bulk OUT TX + ConfigEP + 事件匹配
 */
#include "Compiler.h"
#include "XHCI/Serial/Cdc/Private.h"

static UINT8 gCdcTxBuf[64] BLOCKS_ALIGNED(64);
static volatile UINT32 gCdcBulkDone;
static volatile UINT32 gCdcBulkCode;
static int gCdcTxBusy;

static int ConfigureBulk(UINT32 SlotId, UINT32 RootPort, UINT8 Speed,
                      UINT8 EndpointIn, UINT16 MaxPacketSizeIn, UINT8 EndpointOut, UINT16 MaxPacketSizeOut) {
    UINT8 InNum = EndpointIn & 0x0F;
    UINT8 OutNum = EndpointOut & 0x0F;
    UINT32 InDci = (UINT32)InNum * 2 + 1;
    UINT32 OutDci = (UINT32)OutNum * 2 + 0;
    UINT32 CtxEntries = InDci > OutDci ? InDci : OutDci;
    UINT32 *Slot;
    UINT32 *Endpoint;
    UINT64 Deq;

    if (MaxPacketSizeIn == 0 || MaxPacketSizeIn > 512) {
        MaxPacketSizeIn = 64;
    }
    if (MaxPacketSizeOut == 0 || MaxPacketSizeOut > 512) {
        MaxPacketSizeOut = 64;
    }

    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << InDci) | (1u << OutDci);

    Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | (gCdcRoute & 0xFFFFFu);
    Slot[1] = (UINT32)RootPort << 16;
    if (gCdcHubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)gCdcHubSlot | ((UINT32)gCdcTtPort << 8);
    }

    RingInitialize(gCdcBulkInRing, &gCdcBulkIn, RING_SIZE);
    RingInitialize(gCdcBulkOutRing, &gCdcBulkOut, RING_SIZE);

    Endpoint = (UINT32 *)(void *)InEndpoint(InDci);
    Endpoint[0] = 0;
    Endpoint[1] = (3u << 1) | (6u << 3) | ((UINT32)MaxPacketSizeIn << 16);
    Deq = PointerToPhysical(gCdcBulkInRing) | 1;
    Endpoint[2] = (UINT32)Deq;
    Endpoint[3] = (UINT32)(Deq >> 32);
    Endpoint[4] = (UINT32)MaxPacketSizeIn;

    Endpoint = (UINT32 *)(void *)InEndpoint(OutDci);
    Endpoint[0] = 0;
    Endpoint[1] = (3u << 1) | (2u << 3) | ((UINT32)MaxPacketSizeOut << 16);
    Deq = PointerToPhysical(gCdcBulkOutRing) | 1;
    Endpoint[2] = (UINT32)Deq;
    Endpoint[3] = (UINT32)(Deq >> 32);
    Endpoint[4] = (UINT32)MaxPacketSizeOut;

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gCdcBulkInRing, sizeof(gCdcBulkInRing));
    FlushDma(gCdcBulkOutRing, sizeof(gCdcBulkOutRing));
    FlushDma(gCdcDevCtx, sizeof(gCdcDevCtx));
    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) <
        0) {
        BootLog("Boot: USB-UART CDC Cfg Endpoint Fail\n");
        return 0;
    }
    gCdcBulkInDci = InDci;
    gCdcBulkOutDci = OutDci;
    gCdcBulkInMaxPacketSize = MaxPacketSizeIn;
    gCdcBulkOutMaxPacketSize = MaxPacketSizeOut;
    return 1;
}

static int MatchTransfer(UINT32 EvtSlot, UINT32 Endpoint, UINT64 TrbPtr, UINT32 Code,
                          UINT32 Remain) {
    UINT64 OutLo;
    UINT64 OutHi;
    UINT64 InLo;
    UINT64 InHi;

    if (!gCdcClaimed || gCdcSlot == 0 || EvtSlot != gCdcSlot) {
        return 0;
    }
    OutLo = PointerToPhysical(gCdcBulkOutRing);
    OutHi = OutLo + sizeof(gCdcBulkOutRing);
    InLo = PointerToPhysical(gCdcBulkInRing);
    InHi = InLo + sizeof(gCdcBulkInRing);
    if ((gCdcBulkOutDci != 0 && Endpoint == gCdcBulkOutDci) ||
        (TrbPtr >= OutLo && TrbPtr < OutHi)) {
        gCdcBulkCode = Code;
        gCdcBulkDone = 1;
        return 1;
    }
    if ((gCdcBulkInDci != 0 && Endpoint == gCdcBulkInDci) ||
        (TrbPtr >= InLo && TrbPtr < InHi)) {
        return gCdc.OnComplete ? gCdc.OnComplete(Code, Remain) : 0;
    }
    return 0;
}

static int WaitBulk(void) {
    int Own = 0;
    int Result = -1;
    UINT64 T0 = ReadTsc();
    UINT64 Need = 200ULL * 3000000ULL;

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
        if (gCdcBulkDone) {
            Result = (gCdcBulkCode == CC_SUCCESS || gCdcBulkCode == CC_SHORT_PACKET)
                         ? 0
                         : -1;
            break;
        }
        if (ReadTsc() - T0 >= Need) {
            break;
        }
        __asm__ volatile("pause");
    }
    if (Own) {
        EventLeaveExclusive();
    }
    return Result;
}

static void Write(const char *Text) {
    UINT32 MaxPacketSize;
    UINT32 N;

    if (!Text || !gCdcClaimed || gCdcSlot == 0 || gCdcBulkOutDci == 0) {
        return;
    }
    if (gCdcTxBusy) {
        return;
    }
    gCdcTxBusy = 1;
    MaxPacketSize = gCdcBulkOutMaxPacketSize ? gCdcBulkOutMaxPacketSize : 64;
    if (MaxPacketSize > sizeof(gCdcTxBuf)) {
        MaxPacketSize = (UINT32)sizeof(gCdcTxBuf);
    }

    while (*Text) {
        N = 0;
        while (Text[N] && N < MaxPacketSize) {
            gCdcTxBuf[N] = (UINT8)Text[N];
            N++;
        }
        Text += N;
        FlushDma(gCdcTxBuf, N);
        gCdcBulkDone = 0;
        gCdcBulkCode = 0;
        EventEnterExclusive();
        Enqueue(gCdcBulkOutRing, &gCdcBulkOut, PointerToPhysical(gCdcTxBuf), N,
                TRB_TYPE(TRB_NORMAL) | TRB_IOC);
        FlushDma(gCdcBulkOutRing, sizeof(gCdcBulkOutRing));
        RingDoorbell(gCdcSlot, gCdcBulkOutDci);
        if (WaitBulk() < 0) {
            EventLeaveExclusive();
            break;
        }
        EventLeaveExclusive();
    }
    gCdcTxBusy = 0;
}

void CdcFillTransmit(void) {
    gCdc.ConfigureBulk = ConfigureBulk;
    gCdc.MatchTransfer = MatchTransfer;
    gCdcUart.Write = Write;
}
