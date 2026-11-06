/*
 * FtdiRx.c — PR-H-usb-uart-ftdi-2：Bulk IN + 剥 2 字节状态 → 字符环
 */
#include "Compiler.h"
#include "XHCI/Serial/Ftdi/Private.h"

#define FTDI_RX_Q   256
#define FTDI_STATUS 2

UINT32 gFtdiBulkInDci;
UINT16 gFtdiBulkInMaxPacketSize;
XHCI_TRB gFtdiBulkInRing[RING_SIZE] BLOCKS_ALIGNED(64);
RING_STATE gFtdiBulkIn;

static UINT8 gFtdiRxDma[64] BLOCKS_ALIGNED(64);
static char gFtdiRxQ[FTDI_RX_Q];
static UINT32 gFtdiRxR;
static UINT32 gFtdiRxW;
static volatile UINT32 gFtdiInDone;
static volatile UINT32 gFtdiInCode;
static volatile UINT32 gFtdiInRemain;
static UINT32 gFtdiInTransferLength;
static int gFtdiInArmed;
static int gFtdiRxBusy;

static void ReceivePush(char C) {
    UINT32 Next = (gFtdiRxW + 1u) % FTDI_RX_Q;

    if (Next == gFtdiRxR) {
        return; /* 满则丢，勿堵 USB */
    }
    gFtdiRxQ[gFtdiRxW] = C;
    gFtdiRxW = Next;
}

static int DataReady(void) {
    return (gFtdiClaimed && gFtdiRxR != gFtdiRxW) ? 1 : 0;
}

static char ReadChar(void) {
    char C;

    if (gFtdiRxR == gFtdiRxW) {
        return 0;
    }
    C = gFtdiRxQ[gFtdiRxR];
    gFtdiRxR = (gFtdiRxR + 1u) % FTDI_RX_Q;
    return C;
}

static int OnComplete(UINT32 Code, UINT32 Remain) {
    gFtdiInCode = Code;
    gFtdiInRemain = Remain;
    gFtdiInDone = 1;
    return 1;
}

/*
 * ConfigEP：Bulk IN + OUT（ftdi-1 OUT + ftdi-2 IN 一次配齐）。
 */
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
    Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | (gFtdiRoute & 0xFFFFFu);
    Slot[1] = (UINT32)RootPort << 16;
    if (gFtdiHubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)gFtdiHubSlot | ((UINT32)gFtdiTtPort << 8);
    }

    RingInitialize(gFtdiBulkInRing, &gFtdiBulkIn, RING_SIZE);
    RingInitialize(gFtdiBulkOutRing, &gFtdiBulkOut, RING_SIZE);

    Endpoint = (UINT32 *)(void *)InEndpoint(InDci);
    Endpoint[0] = 0;
    Endpoint[1] = (3u << 1) | (6u << 3) | ((UINT32)MaxPacketSizeIn << 16);
    Deq = PointerToPhysical(gFtdiBulkInRing) | 1;
    Endpoint[2] = (UINT32)Deq;
    Endpoint[3] = (UINT32)(Deq >> 32);
    Endpoint[4] = (UINT32)MaxPacketSizeIn;

    Endpoint = (UINT32 *)(void *)InEndpoint(OutDci);
    Endpoint[0] = 0;
    Endpoint[1] = (3u << 1) | (2u << 3) | ((UINT32)MaxPacketSizeOut << 16);
    Deq = PointerToPhysical(gFtdiBulkOutRing) | 1;
    Endpoint[2] = (UINT32)Deq;
    Endpoint[3] = (UINT32)(Deq >> 32);
    Endpoint[4] = (UINT32)MaxPacketSizeOut;

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gFtdiBulkInRing, sizeof(gFtdiBulkInRing));
    FlushDma(gFtdiBulkOutRing, sizeof(gFtdiBulkOutRing));
    FlushDma(gFtdiDevCtx, sizeof(gFtdiDevCtx));
    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) <
        0) {
        BootLog("Boot: USB-UART Cfg Endpoint Fail\n");
        return 0;
    }
    gFtdiBulkInDci = InDci;
    gFtdiBulkOutDci = OutDci;
    gFtdiBulkInMaxPacketSize = MaxPacketSizeIn;
    gFtdiBulkOutMaxPacketSize = MaxPacketSizeOut;
    gFtdiInArmed = 0;
    gFtdiInDone = 0;
    return 1;
}

static void QueueBulkIn(void) {
    UINT32 Len = gFtdiBulkInMaxPacketSize ? gFtdiBulkInMaxPacketSize : 64;

    if (Len > sizeof(gFtdiRxDma)) {
        Len = (UINT32)sizeof(gFtdiRxDma);
    }
    gFtdiInTransferLength = Len;
    gFtdiInDone = 0;
    gFtdiInCode = 0;
    gFtdiInRemain = 0;
    FlushDma(gFtdiRxDma, Len);
    EventEnterExclusive();
    Enqueue(gFtdiBulkInRing, &gFtdiBulkIn, PointerToPhysical(gFtdiRxDma), Len,
            TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
    FlushDma(gFtdiBulkInRing, sizeof(gFtdiBulkInRing));
    RingDoorbell(gFtdiSlot, gFtdiBulkInDci);
    EventLeaveExclusive();
    gFtdiInArmed = 1;
}

static void DrainInCompletion(void) {
    UINT32 Got;
    UINT32 i;

    if (!gFtdiInDone) {
        return;
    }
    gFtdiInDone = 0;
    gFtdiInArmed = 0;
    if (gFtdiInCode != CC_SUCCESS && gFtdiInCode != CC_SHORT_PACKET) {
        return;
    }
    Got = gFtdiInTransferLength;
    if (gFtdiInRemain <= Got) {
        Got = Got - gFtdiInRemain;
    }
    FlushDma(gFtdiRxDma, Got ? Got : 1);
    /* FTDI：前 2 字节状态；仅状态包时 Got==2 → 无载荷 */
    if (Got > FTDI_STATUS) {
        for (i = FTDI_STATUS; i < Got; i++) {
            ReceivePush((char)gFtdiRxDma[i]);
        }
    }
}

static void PollReceive(void) {
    int Own = 0;

    if (!gFtdiClaimed || gFtdiSlot == 0 || gFtdiBulkInDci == 0) {
        return;
    }
    if (gFtdiRxBusy) {
        return;
    }
    gFtdiRxBusy = 1;

    if (!EventIsExclusive()) {
        EventEnterExclusive();
        Own = 1;
    }
    if (HalCpuIsHypervisor()) {
        ProcessEvents();
    } else {
        ProcessEventsRealPc();
    }
    ServiceHidCompletions();
    if (Own) {
        EventLeaveExclusive();
    }

    DrainInCompletion();
    if (!gFtdiInArmed) {
        QueueBulkIn();
    }
    gFtdiRxBusy = 0;
}

static void Arm(void) {
    gFtdiRxR = 0;
    gFtdiRxW = 0;
    gFtdiInArmed = 0;
    gFtdiInDone = 0;
    if (gFtdiClaimed && gFtdiBulkInDci != 0) {
        QueueBulkIn();
    }
}

void FtdiFillReceive(void) {
    gFtdi.ConfigureBulk = ConfigureBulk;
    gFtdi.ArmReceive = Arm;
    gFtdi.OnComplete = OnComplete;
    gFtdiUart.PollReceive = PollReceive;
    gFtdiUart.DataReady = DataReady;
    gFtdiUart.ReadChar = ReadChar;
}
