/*
 * CdcRx.c — PR-H-usb-uart-cdc-2：CDC Bulk IN → 字符环（无状态前缀）
 */
#include "XHCI/Internal.h"

#define CDC_RX_Q 256

extern UINT32 gCdcBulkInDci;
extern UINT16 gCdcBulkInMps;
extern XHCI_TRB gCdcBulkInRing[RING_SIZE];
extern RING_STATE gCdcBulkIn;

static UINT8 gCdcRxDma[64] __attribute__((aligned(64)));
static char gCdcRxQ[CDC_RX_Q];
static UINT32 gCdcRxR;
static UINT32 gCdcRxW;
static volatile UINT32 gCdcInDone;
static volatile UINT32 gCdcInCode;
static volatile UINT32 gCdcInRemain;
static UINT32 gCdcInXferLen;
static int gCdcInArmed;
static int gCdcRxBusy;

static void RxPush(char C) {
    UINT32 Next = (gCdcRxW + 1u) % CDC_RX_Q;

    if (Next == gCdcRxR) {
        return;
    }
    gCdcRxQ[gCdcRxW] = C;
    gCdcRxW = Next;
}

int CdcDataReady(void) {
    return (gCdcClaimed && gCdcRxR != gCdcRxW) ? 1 : 0;
}

char CdcReadChar(void) {
    char C;

    if (gCdcRxR == gCdcRxW) {
        return 0;
    }
    C = gCdcRxQ[gCdcRxR];
    gCdcRxR = (gCdcRxR + 1u) % CDC_RX_Q;
    return C;
}

int CdcOnInComplete(UINT32 Code, UINT32 Remain) {
    gCdcInCode = Code;
    gCdcInRemain = Remain;
    gCdcInDone = 1;
    return 1;
}

static void QueueBulkIn(void) {
    UINT32 Len = gCdcBulkInMps ? gCdcBulkInMps : 64;

    if (Len > sizeof(gCdcRxDma)) {
        Len = (UINT32)sizeof(gCdcRxDma);
    }
    gCdcInXferLen = Len;
    gCdcInDone = 0;
    gCdcInCode = 0;
    gCdcInRemain = 0;
    FlushDma(gCdcRxDma, Len);
    EventEnterExclusive();
    Enqueue(gCdcBulkInRing, &gCdcBulkIn, PointerToPhysical(gCdcRxDma), Len,
            TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
    FlushDma(gCdcBulkInRing, sizeof(gCdcBulkInRing));
    RingDoorbell(gCdcSlot, gCdcBulkInDci);
    EventLeaveExclusive();
    gCdcInArmed = 1;
}

static void DrainInCompletion(void) {
    UINT32 Got;
    UINT32 i;

    if (!gCdcInDone) {
        return;
    }
    gCdcInDone = 0;
    gCdcInArmed = 0;
    if (gCdcInCode != CC_SUCCESS && gCdcInCode != CC_SHORT_PACKET) {
        return;
    }
    Got = gCdcInXferLen;
    if (gCdcInRemain <= Got) {
        Got = Got - gCdcInRemain;
    }
    FlushDma(gCdcRxDma, Got ? Got : 1);
    /* CDC-ACM：整包都是载荷（无 FTDI 那 2 字节状态） */
    for (i = 0; i < Got; i++) {
        RxPush((char)gCdcRxDma[i]);
    }
}

void CdcPollRx(void) {
    int Own = 0;

    if (!gCdcClaimed || gCdcSlot == 0 || gCdcBulkInDci == 0) {
        return;
    }
    if (gCdcRxBusy) {
        return;
    }
    gCdcRxBusy = 1;

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
    if (!gCdcInArmed) {
        QueueBulkIn();
    }
    gCdcRxBusy = 0;
}

void CdcRxArm(void) {
    gCdcRxR = 0;
    gCdcRxW = 0;
    gCdcInArmed = 0;
    gCdcInDone = 0;
    if (gCdcClaimed && gCdcBulkInDci != 0) {
        QueueBulkIn();
    }
}
